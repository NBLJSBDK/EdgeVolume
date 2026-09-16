#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDir>
#include <QHash>
#include <QSet>
#include <QSocketNotifier>
#include <QTimer>

#include <cerrno>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

bool hasBit(const QByteArray &bits, int bit)
{
    const int byte = bit / 8;
    if (byte < 0 || byte >= bits.size()) {
        return false;
    }

    const auto value = static_cast<unsigned char>(bits.at(byte));
    return (value & (1u << (bit % 8))) != 0;
}

bool deviceHasWheel(int fd, bool *hiRes)
{
    QByteArray eventBits((EV_MAX + 8) / 8, '\0');
    if (ioctl(fd, EVIOCGBIT(0, EV_MAX + 1), eventBits.data()) < 0 ||
        !hasBit(eventBits, EV_REL)) {
        return false;
    }

    QByteArray relativeBits((REL_MAX + 8) / 8, '\0');
    if (ioctl(fd, EVIOCGBIT(EV_REL, REL_MAX + 1), relativeBits.data()) < 0 ||
        !hasBit(relativeBits, REL_WHEEL)) {
        return false;
    }

#ifdef REL_WHEEL_HI_RES
    *hiRes = hasBit(relativeBits, REL_WHEEL_HI_RES);
#else
    *hiRes = false;
#endif
    return true;
}

class EdgeVolumeDaemon;

class CursorAdaptor : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.example.EdgeVolume")

public:
    explicit CursorAdaptor(EdgeVolumeDaemon *daemon)
        : m_daemon(daemon)
    {
    }

public slots:
    void setCursor(int x, int y, int edgeX);

private:
    EdgeVolumeDaemon *m_daemon;
};

class EdgeVolumeDaemon : public QObject
{
    Q_OBJECT

public:
    explicit EdgeVolumeDaemon(QObject *parent = nullptr)
        : QObject(parent)
        , m_cursorAdaptor(this)
    {
        auto bus = QDBusConnection::sessionBus();
        if (!bus.registerService(QStringLiteral("org.example.EdgeVolume"))) {
            qCritical("无法占用 D-Bus 服务 org.example.EdgeVolume：%s",
                      qPrintable(bus.lastError().message()));
            return;
        }

        if (!bus.registerObject(QStringLiteral("/EdgeVolume"),
                                &m_cursorAdaptor,
                                QDBusConnection::ExportAllSlots)) {
            qCritical("无法注册 D-Bus 对象：%s",
                      qPrintable(bus.lastError().message()));
            bus.unregisterService(QStringLiteral("org.example.EdgeVolume"));
            return;
        }

        m_busReady = true;
        scanDevices();

        auto *timer = new QTimer(this);
        timer->setInterval(2000);
        connect(timer, &QTimer::timeout, this, &EdgeVolumeDaemon::scanDevices);
        timer->start();
    }

    bool isReady() const
    {
        return m_busReady;
    }

    void setCursor(int x, int y, int edgeX)
    {
        Q_UNUSED(y)
        m_cursorX = x;
        m_edgeX = edgeX;
        m_haveCursor = true;

        if (!m_reportedCursor) {
            qInfo().noquote() << "KWin 光标桥接已连接，当前 x=" << x
                              << "，左边界=" << edgeX;
            m_reportedCursor = true;
        }
    }

private:
    struct Device {
        int fd = -1;
        bool hiRes = false;
        int hiResRemainder = 0;
        QSocketNotifier *notifier = nullptr;
    };

    void scanDevices()
    {
        QSet<QString> seen;
        const auto entries = QDir(QStringLiteral("/dev/input"))
                                 .entryInfoList({QStringLiteral("event*")},
                                                QDir::AllEntries | QDir::System |
                                                    QDir::NoDotAndDotDot,
                                                QDir::Name);

        for (const auto &entry : entries) {
            const QString path = entry.absoluteFilePath();
            seen.insert(path);

            if (m_devices.contains(path)) {
                continue;
            }

            const int fd = ::open(path.toLocal8Bit().constData(),
                                   O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) {
                if (!m_warnedPaths.contains(path)) {
                    qWarning().noquote()
                        << "无法打开输入设备：" << path << "："
                        << QString::fromLocal8Bit(std::strerror(errno));
                    m_warnedPaths.insert(path);
                }
                continue;
            }

            m_warnedPaths.remove(path);

            bool hiRes = false;
            if (!deviceHasWheel(fd, &hiRes)) {
                ::close(fd);
                continue;
            }

            Device device;
            device.fd = fd;
            device.hiRes = hiRes;
            device.notifier = new QSocketNotifier(fd,
                                                   QSocketNotifier::Read,
                                                   this);

            connect(device.notifier,
                    &QSocketNotifier::activated,
                    this,
                    [this, path](QSocketDescriptor, QSocketNotifier::Type) {
                        readDevice(path);
                    });

            m_devices.insert(path, device);
            qInfo().noquote() << "监听滚轮设备：" << path
                              << (hiRes ? "(高分辨率)" : "(标准)");
        }

        if (m_devices.isEmpty() && !m_reportedNoDevices) {
            qWarning().noquote()
                << "没有找到可读取的滚轮设备。请检查 /dev/input/event* 权限。";
            m_reportedNoDevices = true;
        } else if (!m_devices.isEmpty()) {
            m_reportedNoDevices = false;
        }

        for (auto it = m_devices.begin(); it != m_devices.end();) {
            if (seen.contains(it.key())) {
                ++it;
                continue;
            }

            if (it.value().notifier) {
                it.value().notifier->deleteLater();
            }
            ::close(it.value().fd);
            it = m_devices.erase(it);
        }
    }

    void readDevice(const QString &path)
    {
        auto it = m_devices.find(path);
        if (it == m_devices.end()) {
            return;
        }

        input_event event;
        while (true) {
            const ssize_t count = ::read(it.value().fd, &event, sizeof(event));
            if (count == static_cast<ssize_t>(sizeof(event))) {
                if (event.type != EV_REL) {
                    continue;
                }

#ifdef REL_WHEEL_HI_RES
                if (it.value().hiRes && event.code == REL_WHEEL_HI_RES) {
                    it.value().hiResRemainder += event.value;
                    while (it.value().hiResRemainder >= 120) {
                        changeVolume(+1);
                        it.value().hiResRemainder -= 120;
                    }
                    while (it.value().hiResRemainder <= -120) {
                        changeVolume(-1);
                        it.value().hiResRemainder += 120;
                    }
                    continue;
                }
#endif

                if (!it.value().hiRes && event.code == REL_WHEEL) {
                    const int direction = event.value > 0 ? 1 : -1;
                    for (int i = 0; i < qAbs(event.value); ++i) {
                        changeVolume(direction);
                    }
                }
                continue;
            }

            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                return;
            }

            return;
        }
    }

    void changeVolume(int direction)
    {
        if (!m_haveCursor || m_cursorX != m_edgeX) {
            return;
        }

        QDBusMessage message = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.kglobalaccel"),
            QStringLiteral("/component/kmix"),
            QStringLiteral("org.kde.kglobalaccel.Component"),
            QStringLiteral("invokeShortcut"));

        message << (direction > 0 ? QStringLiteral("increase_volume")
                                  : QStringLiteral("decrease_volume"));
        QDBusConnection::sessionBus().asyncCall(message);
    }

    QHash<QString, Device> m_devices;
    CursorAdaptor m_cursorAdaptor;
    int m_cursorX = 0;
    int m_edgeX = 0;
    bool m_haveCursor = false;
    bool m_busReady = false;
    bool m_reportedCursor = false;
    bool m_reportedNoDevices = false;
    QSet<QString> m_warnedPaths;

    friend class CursorAdaptor;
};

void CursorAdaptor::setCursor(int x, int y, int edgeX)
{
    m_daemon->setCursor(x, y, edgeX);
}

} // namespace

#include "main.moc"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("edge-volume"));

    qInfo().noquote()
        << "edge-volume 已启动：无窗口监听模式"
        << "\n仅当光标位于屏幕最左侧 1px 时处理滚轮"
        << "\n点击不会被拦截";

    EdgeVolumeDaemon daemon;
    if (!daemon.isReady()) {
        return 1;
    }
    return app.exec();
}

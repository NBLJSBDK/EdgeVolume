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
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

constexpr char kVirtualDevicePrefix[] = "EdgeVolume Virtual ";

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

QString deviceName(int fd)
{
    char name[256] = {};
    if (::ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) {
        return {};
    }
    return QString::fromLocal8Bit(name);
}

bool setUinputCapability(int fd, unsigned long request, int code)
{
    return ::ioctl(fd, request, code) == 0;
}

bool configureVirtualDevice(int physicalFd, int virtualFd, const QString &name)
{
    QByteArray eventBits((EV_MAX + 8) / 8, '\0');
    if (::ioctl(physicalFd, EVIOCGBIT(0, EV_MAX + 1), eventBits.data()) < 0) {
        return false;
    }

    uinput_user_dev userDevice = {};
    const QByteArray virtualName =
        QByteArray(kVirtualDevicePrefix) + name.toUtf8();
    std::strncpy(userDevice.name,
                 virtualName.constData(),
                 sizeof(userDevice.name) - 1);

    input_id id = {};
    if (::ioctl(physicalFd, EVIOCGID, &id) == 0) {
        userDevice.id = id;
    }

    if (hasBit(eventBits, EV_KEY)) {
        if (!setUinputCapability(virtualFd, UI_SET_EVBIT, EV_KEY)) {
            return false;
        }
        QByteArray bits((KEY_MAX + 8) / 8, '\0');
        if (::ioctl(physicalFd, EVIOCGBIT(EV_KEY, KEY_MAX + 1), bits.data()) < 0) {
            return false;
        }
        for (int code = 0; code <= KEY_MAX; ++code) {
            if (hasBit(bits, code) &&
                !setUinputCapability(virtualFd, UI_SET_KEYBIT, code)) {
                return false;
            }
        }
    }

    if (hasBit(eventBits, EV_REL)) {
        if (!setUinputCapability(virtualFd, UI_SET_EVBIT, EV_REL)) {
            return false;
        }
        QByteArray bits((REL_MAX + 8) / 8, '\0');
        if (::ioctl(physicalFd, EVIOCGBIT(EV_REL, REL_MAX + 1), bits.data()) < 0) {
            return false;
        }
        for (int code = 0; code <= REL_MAX; ++code) {
            if (hasBit(bits, code) &&
                !setUinputCapability(virtualFd, UI_SET_RELBIT, code)) {
                return false;
            }
        }
    }

    if (hasBit(eventBits, EV_ABS)) {
        if (!setUinputCapability(virtualFd, UI_SET_EVBIT, EV_ABS)) {
            return false;
        }
        QByteArray bits((ABS_MAX + 8) / 8, '\0');
        if (::ioctl(physicalFd, EVIOCGBIT(EV_ABS, ABS_MAX + 1), bits.data()) < 0) {
            return false;
        }
        for (int code = 0; code <= ABS_MAX; ++code) {
            if (!hasBit(bits, code)) {
                continue;
            }
            if (!setUinputCapability(virtualFd, UI_SET_ABSBIT, code)) {
                return false;
            }
            input_absinfo info = {};
            if (::ioctl(physicalFd, EVIOCGABS(code), &info) == 0) {
                userDevice.absmin[code] = info.minimum;
                userDevice.absmax[code] = info.maximum;
                userDevice.absfuzz[code] = info.fuzz;
                userDevice.absflat[code] = info.flat;
            }
        }
    }

    if (hasBit(eventBits, EV_MSC)) {
        if (!setUinputCapability(virtualFd, UI_SET_EVBIT, EV_MSC)) {
            return false;
        }
        QByteArray bits((MSC_MAX + 8) / 8, '\0');
        if (::ioctl(physicalFd, EVIOCGBIT(EV_MSC, MSC_MAX + 1), bits.data()) < 0) {
            return false;
        }
        for (int code = 0; code <= MSC_MAX; ++code) {
            if (hasBit(bits, code) &&
                !setUinputCapability(virtualFd, UI_SET_MSCBIT, code)) {
                return false;
            }
        }
    }

    if (hasBit(eventBits, EV_SW)) {
        if (!setUinputCapability(virtualFd, UI_SET_EVBIT, EV_SW)) {
            return false;
        }
        QByteArray bits((SW_MAX + 8) / 8, '\0');
        if (::ioctl(physicalFd, EVIOCGBIT(EV_SW, SW_MAX + 1), bits.data()) < 0) {
            return false;
        }
        for (int code = 0; code <= SW_MAX; ++code) {
            if (hasBit(bits, code) &&
                !setUinputCapability(virtualFd, UI_SET_SWBIT, code)) {
                return false;
            }
        }
    }

    QByteArray propertyBits((INPUT_PROP_MAX + 8) / 8, '\0');
    if (::ioctl(physicalFd,
                EVIOCGPROP(INPUT_PROP_MAX + 1),
                propertyBits.data()) == 0) {
        for (int property = 0; property <= INPUT_PROP_MAX; ++property) {
            if (hasBit(propertyBits, property) &&
                !setUinputCapability(virtualFd, UI_SET_PROPBIT, property)) {
                return false;
            }
        }
    }

    return ::write(virtualFd, &userDevice, sizeof(userDevice)) ==
           static_cast<ssize_t>(sizeof(userDevice));
}

int createVirtualDevice(int physicalFd, const QString &name)
{
    const int virtualFd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (virtualFd < 0) {
        return -1;
    }

    if (!configureVirtualDevice(physicalFd, virtualFd, name) ||
        ::ioctl(virtualFd, UI_DEV_CREATE) < 0) {
        ::close(virtualFd);
        return -1;
    }

    return virtualFd;
}

void destroyVirtualDevice(int virtualFd)
{
    if (virtualFd < 0) {
        return;
    }
    ::ioctl(virtualFd, UI_DEV_DESTROY);
    ::close(virtualFd);
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

    ~EdgeVolumeDaemon() override
    {
        for (auto it = m_devices.begin(); it != m_devices.end(); ++it) {
            releaseDevice(it.value());
        }
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
        int virtualFd = -1;
        bool hiRes = false;
        int hiResRemainder = 0;
        bool grabbed = false;
        bool warnedWrite = false;
        QSocketNotifier *notifier = nullptr;
    };

    void releaseDevice(Device &device)
    {
        if (device.notifier) {
            device.notifier->setEnabled(false);
            device.notifier->deleteLater();
            device.notifier = nullptr;
        }
        if (device.grabbed) {
            ::ioctl(device.fd, EVIOCGRAB, 0);
            device.grabbed = false;
        }
        destroyVirtualDevice(device.virtualFd);
        device.virtualFd = -1;
        if (device.fd >= 0) {
            ::close(device.fd);
            device.fd = -1;
        }
    }

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

            const QString name = deviceName(fd);
            if (name.startsWith(QLatin1String(kVirtualDevicePrefix))) {
                ::close(fd);
                continue;
            }

            bool hiRes = false;
            if (!deviceHasWheel(fd, &hiRes)) {
                ::close(fd);
                continue;
            }

            const int virtualFd = createVirtualDevice(fd, name);
            if (virtualFd < 0) {
                if (!m_warnedUinput) {
                    qWarning().noquote()
                        << "无法创建虚拟鼠标 /dev/uinput："
                        << QString::fromLocal8Bit(std::strerror(errno));
                    m_warnedUinput = true;
                }
                ::close(fd);
                continue;
            }

            if (::ioctl(fd, EVIOCGRAB, 1) < 0) {
                qWarning().noquote()
                    << "无法独占输入设备：" << path << "："
                    << QString::fromLocal8Bit(std::strerror(errno));
                destroyVirtualDevice(virtualFd);
                ::close(fd);
                continue;
            }

            Device device;
            device.fd = fd;
            device.virtualFd = virtualFd;
            device.hiRes = hiRes;
            device.grabbed = true;
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
            qInfo().noquote() << "拦截滚轮设备：" << path
                              << "，点击/移动转发到虚拟鼠标"
                              << (hiRes ? "(高分辨率)" : "(标准)");
        }

        if (m_devices.isEmpty() && !m_reportedNoDevices) {
            qWarning().noquote()
                << "没有找到可拦截的滚轮设备。请检查 /dev/input/event* 和 /dev/uinput 权限。";
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
                it.value().notifier->setEnabled(false);
            }
            releaseDevice(it.value());
            it = m_devices.erase(it);
        }
    }

    bool isAtEdge() const
    {
        return m_haveCursor && m_cursorX == m_edgeX;
    }

    void forwardEvent(Device &device, const input_event &event)
    {
        if (::write(device.virtualFd, &event, sizeof(event)) ==
            static_cast<ssize_t>(sizeof(event))) {
            return;
        }

        if (!device.warnedWrite) {
            qWarning().noquote()
                << "无法向虚拟鼠标转发输入事件："
                << QString::fromLocal8Bit(std::strerror(errno));
            device.warnedWrite = true;
        }
    }

    void readDevice(const QString &path)
    {
        auto it = m_devices.find(path);
        if (it == m_devices.end()) {
            return;
        }

        Device &device = it.value();
        input_event event;
        while (true) {
            const ssize_t count = ::read(device.fd, &event, sizeof(event));
            if (count == static_cast<ssize_t>(sizeof(event))) {
#ifdef REL_WHEEL_HI_RES
                if (event.type == EV_REL &&
                    device.hiRes && event.code == REL_WHEEL_HI_RES) {
                    if (isAtEdge()) {
                        device.hiResRemainder += event.value;
                        while (device.hiResRemainder >= 120) {
                            changeVolume(+1);
                            device.hiResRemainder -= 120;
                        }
                        while (device.hiResRemainder <= -120) {
                            changeVolume(-1);
                            device.hiResRemainder += 120;
                        }
                    } else {
                        device.hiResRemainder = 0;
                        forwardEvent(device, event);
                    }
                    continue;
                }
#endif

                if (event.type == EV_REL && event.code == REL_WHEEL) {
                    if (isAtEdge()) {
                        if (!device.hiRes) {
                            const int direction = event.value > 0 ? 1 : -1;
                            const int steps = event.value >= 0 ? event.value : -event.value;
                            for (int i = 0; i < steps; ++i) {
                                changeVolume(direction);
                            }
                        }
                    } else {
                        forwardEvent(device, event);
                    }
                    continue;
                }

                // EVIOCGRAB 会抓住整只物理鼠标；除边缘滚轮外，所有事件
                // 都原样转发到虚拟鼠标，保证点击、移动和拖拽继续可用。
                forwardEvent(device, event);
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
        if (!isAtEdge()) {
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
    bool m_warnedUinput = false;
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
        << "edge-volume 已启动：滚轮拦截模式"
        << "\n左边缘滚轮由 EdgeVolume 消费并调节 KDE 音量"
        << "\n点击、移动和拖拽通过虚拟鼠标转发";

    EdgeVolumeDaemon daemon;
    if (!daemon.isReady()) {
        return 1;
    }
    return app.exec();
}

/*
    KDEVolume - native KWin input filter for KDE Plasma 6.

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <kwin/input.h>
#include <kwin/input_event.h>
#include <kwin/main.h>
#include <kwin/plugin.h>
#include <kwin/workspace.h>
#include <kwin/x11eventfilter.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDebug>
#include <QList>

#include <cstdint>
#include <memory>
#include <xcb/xcb.h>
#include <xcb/xcbext.h>

namespace
{

#if KWIN_BUILD_X11
struct XInputButtonEvent
{
    uint8_t responseType;
    uint8_t extension;
    uint16_t sequence;
    uint32_t length;
    uint16_t eventType;
    uint16_t deviceId;
    uint32_t time;
    uint32_t detail;
    uint32_t root;
    uint32_t event;
    uint32_t child;
    uint32_t fullSequence;
    int32_t rootX;
    int32_t rootY;
    int32_t eventX;
    int32_t eventY;
};

constexpr uint16_t XInputButtonPress = 4;
constexpr uint16_t XInputButtonRelease = 5;

int xInputOpcode()
{
    if (KWin::kwinApp()->operationMode() != KWin::Application::OperationModeX11) {
        return 0;
    }

    static xcb_extension_t extension = {"XInputExtension", 0};
    const auto *reply = xcb_get_extension_data(KWin::kwinApp()->x11Connection(), &extension);
    return reply && reply->present ? reply->major_opcode : 0;
}
#endif

class KDEVolumeFilter final : public KWin::Plugin, public KWin::InputEventFilter
#if KWIN_BUILD_X11
    , public KWin::X11EventFilter
#endif
{
public:
    KDEVolumeFilter()
        : KWin::Plugin()
        , KWin::InputEventFilter(KWin::InputFilterOrder::ScreenEdge)
#if KWIN_BUILD_X11
        , KWin::X11EventFilter(XCB_GE_GENERIC, xInputOpcode(), QList<int>{XInputButtonPress, XInputButtonRelease})
#endif
    {
        KWin::input()->installInputEventFilter(this);
#if KWIN_BUILD_X11
        if (KWin::kwinApp()->operationMode() == KWin::Application::OperationModeX11) {
            KWin::kwinApp()->registerEventFilter(this);
            m_x11Registered = true;
        }
#endif
    }

    ~KDEVolumeFilter() override
    {
#if KWIN_BUILD_X11
        if (m_x11Registered) {
            KWin::kwinApp()->unregisterEventFilter(this);
        }
#endif
        if (KWin::input()) {
            KWin::input()->uninstallInputEventFilter(this);
        }
    }

#if KWIN_BUILD_X11
    bool event(xcb_generic_event_t *event) override
    {
        const auto *genericEvent = reinterpret_cast<const xcb_ge_generic_event_t *>(event);
        const auto *buttonEvent = reinterpret_cast<const XInputButtonEvent *>(event);
        if (genericEvent->event_type != XInputButtonPress &&
            genericEvent->event_type != XInputButtonRelease) {
            return false;
        }
        if (buttonEvent->detail != 4 && buttonEvent->detail != 5) {
            return false;
        }

        const QPointF position(
            static_cast<qreal>(buttonEvent->rootX) / 65536.0,
            static_cast<qreal>(buttonEvent->rootY) / 65536.0);
        qInfo() << "KDEVolume: XInput2 button"
                << "position=" << position
                << "button=" << buttonEvent->detail
                << "eventType=" << genericEvent->event_type;
        if (!isAtLeftEdge(position)) {
            qInfo() << "KDEVolume: XInput2 wheel passed through (not at left edge)";
            return false;
        }

        if (genericEvent->event_type == XInputButtonPress) {
            changeVolume(buttonEvent->detail == 4 ? +1 : -1);
        }
        return true;
    }
#endif

    bool pointerAxis(KWin::PointerAxisEvent *event) override
    {
        if (event->orientation != Qt::Vertical ||
            event->source == KWin::PointerAxisSource::Finger ||
            event->source == KWin::PointerAxisSource::Continuous) {
            m_scrollV120 = 0;
            return false;
        }

        if (!isAtLeftEdge(event->position)) {
            m_scrollV120 = 0;
            return false;
        }

        if (event->deltaV120 != 0) {
            m_scrollV120 += event->deltaV120;
            while (m_scrollV120 <= -120) {
                changeVolume(+1);
                m_scrollV120 += 120;
            }
            while (m_scrollV120 >= 120) {
                changeVolume(-1);
                m_scrollV120 -= 120;
            }
        } else if (!qFuzzyIsNull(event->delta)) {
            changeVolume(event->delta < 0 ? +1 : -1);
        }

        // Consume the axis event. Buttons are handled by a different callback
        // and are therefore unaffected by this filter.
        return true;
    }

private:
    bool isAtLeftEdge(const QPointF &position) const
    {
        if (!KWin::workspace()) {
            return false;
        }

        const qreal left = KWin::workspace()->geometry().left();
        return position.x() >= left && position.x() < left + 1.0;
    }

    void changeVolume(int direction)
    {
        QDBusMessage message = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.kglobalaccel"),
            QStringLiteral("/component/kmix"),
            QStringLiteral("org.kde.kglobalaccel.Component"),
            QStringLiteral("invokeShortcut"));

        message << (direction > 0 ? QStringLiteral("increase_volume")
                                  : QStringLiteral("decrease_volume"));
        QDBusConnection::sessionBus().asyncCall(message);
    }

    qint32 m_scrollV120 = 0;
#if KWIN_BUILD_X11
    bool m_x11Registered = false;
#endif
};

class KDEVolumeFactory final : public KWin::PluginFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PluginFactory_iid FILE "metadata.json")
    Q_INTERFACES(KWin::PluginFactory)

public:
    std::unique_ptr<KWin::Plugin> create() const override
    {
        return std::make_unique<KDEVolumeFilter>();
    }
};

} // namespace

#include "main.moc"

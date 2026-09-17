/*
    KDEVolume - native KWin input filter for KDE Plasma 6.

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <kwin/input.h>
#include <kwin/input_event.h>
#include <kwin/plugin.h>
#include <kwin/workspace.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDebug>

#include <cmath>
#include <memory>

namespace
{

class KDEVolumeFilter final : public KWin::Plugin, public KWin::InputEventFilter
{
public:
    KDEVolumeFilter()
        : KWin::Plugin()
        , KWin::InputEventFilter(KWin::InputFilterOrder::ScreenEdge)
    {
        KWin::input()->installInputEventFilter(this);
        qInfo() << "KDEVolume: input filter installed";
    }

    ~KDEVolumeFilter() override
    {
        if (KWin::input()) {
            KWin::input()->uninstallInputEventFilter(this);
        }
    }

    bool pointerAxis(KWin::PointerAxisEvent *event) override
    {
        qInfo() << "KDEVolume: axis"
                << "position=" << event->position
                << "delta=" << event->delta
                << "deltaV120=" << event->deltaV120
                << "orientation=" << static_cast<int>(event->orientation)
                << "source=" << static_cast<int>(event->source);

        if (event->orientation != Qt::Vertical ||
            event->source == KWin::PointerAxisSource::Finger ||
            event->source == KWin::PointerAxisSource::Continuous) {
            qInfo() << "KDEVolume: axis passed through (not a mouse wheel)";
            m_scrollV120 = 0;
            return false;
        }

        if (!isAtLeftEdge(event->position)) {
            qInfo() << "KDEVolume: axis passed through (not at left edge)";
            m_scrollV120 = 0;
            return false;
        }

        qInfo() << "KDEVolume: left-edge wheel detected";
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
        qInfo() << "KDEVolume: invoking"
                << (direction > 0 ? "increase_volume" : "decrease_volume")
                << "through KDE global shortcut";

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

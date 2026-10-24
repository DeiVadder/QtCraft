#pragma once

#include "app/controllers/QcScreenState.h"

#include <QObject>
#include <QtQmlIntegration/qqmlintegration.h>

class QcSettingsController;
struct QcSessionParts;

// The pause of a world hosted in the game (decision E-877, GameController.pause in QML). The pause
// belongs to the pause menu as a mode, not to the screen shown: the pause menu and the screens it
// opens (settings, character, LAN, manage server) keep it, also under the handbook opened from
// there, until the player is back in the game. The handbook, the inventory and the other screens
// opened from the game do not pause, nor the debug options opened by their key. With
// "pause when the window loses focus" an inactive window opens the pause menu (when no other screen
// is open) and pauses while it stays inactive. The wish goes to the server (QcMsg::PauseRequest),
// which pauses only while its own player is alone; music and ambience fall silent while it does.
// Lives in the GUI thread beside QcGameController, which owns it.
class QcWorldPause : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(WorldPause)
    QML_UNCREATABLE("Provided by GameController.pause")
    // The server's ticks pause.
    Q_PROPERTY(bool paused READ paused NOTIFY changed FINAL)
    // Why the pause menu does not pause (WorldPause.Hint in QML).
    Q_PROPERTY(QcWorldPause::Hint hint READ hint NOTIFY changed FINAL)

public:
    // None: it pauses, or no pause is wished; OthersConnected: a world hosted here keeps running for
    // the other players connected to it; SharedServer: a server joined over the network never pauses
    // for one player; Pending: the server pauses as soon as the world around the player has arrived
    // (or a connecting player has gone).
    enum class Hint { None, OthersConnected, SharedServer, Pending };
    Q_ENUM(Hint)

    QcWorldPause(const QcSessionParts &parts, QcScreenState &screens, QcSettingsController &settings, QObject *parent = nullptr);

    // The overlays that pause: the pause menu and the screens it opens, without the debug options
    // (their key opens them from the game).
    [[nodiscard]] static bool pausesWorld(QcScreenState::Overlay overlay);

    // Whether the screens and the window wish the pause now.
    bool wanted() const { return m_wanted; }
    bool paused() const;
    Hint hint() const;
    void setWindowActive(bool active);
    // A new session (QcGameController::sessionStarted): it hears the wish, its pause is followed.
    void sessionStarted();

signals:
    void changed();
    // The wish changed; the session of a world hosted here has been told already.
    void wantedChanged(bool wanted);

private:
    void update();
    // Music and ambience follow the server's pause.
    void sessionChanged();

    const QcSessionParts &m_parts;
    QcScreenState &m_screens;
    QcSettingsController &m_settings;
    bool m_windowActive = true;
    bool m_wanted = false;
};

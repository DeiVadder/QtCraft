#pragma once

#include "clientcore/session/QcClientSession.h"

#include <QObject>
#include <QString>

#include <functional>

class QcAudioEngine;
class QcGameInput;
class QcHudState;
class QcPlayerController;
class QcStartupHooks;
class QcTradeController;
class QcWorldAtlas;
struct QcSessionParts;

// The one place where the signals of a new client session reach the GUI (decision E-530): chunks
// and blocks go to the mesh scheduler and its meshes to the world view, sounds and ambience to the
// audio engine, chat to the HUD, inventory, machines, trading and health to the player and trade
// controllers, and the screens they open or close to the in-game screen. Lives in the GUI thread
// beside QcGameController, which owns it; it refers only to parts declared before it.
class QcSessionWiring : public QObject
{
    Q_OBJECT

public:
    // What the session's signals are handed to; all of them outlive this object.
    struct Targets
    {
        QcAudioEngine &audio;
        QcHudState &hud;
        QcPlayerController &player;
        QcTradeController &trade;
        QcGameInput &input;
        QcStartupHooks &startup;
        QcWorldAtlas &atlas;
    };

    // The in-game screen on top of the world (QcGameController::overlay): read and set.
    struct Overlay
    {
        std::function<QString()> get;
        std::function<void(const QString &)> set;
    };

    QcSessionWiring(QcSessionParts &parts, const Targets &targets, Overlay overlay);

    // Wires the new session (parts.session) and its mesh scheduler (parts.meshes).
    void connectSession();

signals:
    // The session is in the world.
    void playing();
    // The session ended; `reason` is the translated reason from the server (E-094).
    void disconnected(const QString &reason);
    // The session ran a tick.
    void ticked();

private:
    void connectWorld();
    void connectPlayer();
    void onSessionState(QcClientSession::State state);
    void onContentReady();
    void onMachineOpened();
    void onMachineClosed();
    void onTradeOpened();
    void onTradeClosed();
    void onHealthChanged();

    QcSessionParts &m_parts;
    Targets m_targets;
    Overlay m_overlay;
};

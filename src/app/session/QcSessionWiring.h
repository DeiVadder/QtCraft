// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/session/QcSessionAudio.h"
#include "clientcore/session/QcClientSession.h"

#include <QObject>
#include <QString>

class QcAdvancementTreeModel;
class QcAudioEngine;
class QcHudState;
class QcPlayerController;
class QcScreenState;
class QcStartupHooks;
class QcStonecutterController;
class QcAnvilController;
class QcEnchantingController;
class QcTradeController;
class QcWorldAtlas;
struct QcSessionParts;

// The one place where the signals of a new client session reach the GUI (decision E-530): chunks
// and blocks go to the mesh scheduler and its meshes to the world view, sounds and ambience to the
// audio engine, chat to the HUD, inventory, machines, trading and health to the player and trade
// controllers, and the screens they open or close and the player's death to the screen state.
// Lives in the GUI thread beside QcGameController, which owns it; it refers only to parts
// declared before it.
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
        QcStonecutterController &stonecutter;
        QcEnchantingController &enchanting;
        QcAnvilController &anvil;
        QcAdvancementTreeModel &advancements;
        QcScreenState &screens;
        QcStartupHooks &startup;
        QcWorldAtlas &atlas;
    };

    QcSessionWiring(QcSessionParts &parts, const Targets &targets);

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
    // The stonecutter screen (decision E-785).
    void connectStonecutter();
    // The enchanting table's screen (decision E-864).
    void connectEnchanting();
    // The anvil's screen (decision E-866).
    void connectAnvil();
    void onSessionState(QcClientSession::State state);
    void onContentReady();
    void onMachineOpened();
    void onMachineClosed();
    void onTradeOpened();
    void onTradeClosed();

    QcSessionParts &m_parts;
    Targets m_targets;
    QcSessionAudio m_audio;
};

#pragma once

#include "app/controllers/QcHandbookController.h"
#include "app/controllers/QcHudState.h"
#include "app/controllers/QcMultiplayerController.h"
#include "app/controllers/QcPhotoMode.h"
#include "app/controllers/QcPlayerController.h"
#include "app/controllers/QcScreenState.h"
#include "app/controllers/QcSettingsController.h"
#include "app/controllers/QcSignController.h"
#include "app/controllers/QcTradeController.h"
#include "app/controllers/QcWorldLibraryController.h"
#include "app/hooks/QcStartupHooks.h"
#include "app/input/QcGameInput.h"
#include "app/input/QcGamepadUi.h"
#include "app/models/QcItemIcons.h"
#include "app/preview/QcPreviewSource.h"
#include "app/rendering/QcRenderBridge.h"
#include "app/session/QcFrameDriver.h"
#include "app/session/QcSessionParts.h"
#include "app/session/QcSessionWiring.h"
#include "app/session/QcWorldAtlas.h"
#include "app/session/QcWorldStarter.h"
#include "clientcore/input/QcGamepadControls.h"
#include "clientcore/session/QcClientSession.h"
#include "common/core/QcResult.h"

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

class QcAudioEngine;
class QcBenchmarkRun;
class QcContentRegistry;
class QcGamepadSource;
class QcItemIconProvider;
class QQuickWindow;
struct QcWorldInfo;

// The game for QML (GameController, `game` in Main.qml): runs a single-player session with its
// in-process server, or a session of a network game, and shows its phase, status and in-game
// screen. Settings, library, player,
// HUD, handbook and input are sub-objects with their own QML properties; the render bridge hands
// the world to the view every frame. Lives in the GUI thread and is created from QML.
class QcGameController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(GameController)
    // GameController.Phase in QML: the enumeration lives in the screen state.
    QML_EXTENDED_NAMESPACE(QcScreenState)

    Q_PROPERTY(QcWorldView *view READ view WRITE setView NOTIFY viewChanged FINAL)
    Q_PROPERTY(QcScreenState::Phase phase READ phase NOTIFY phaseChanged FINAL)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged FINAL)
    Q_PROPERTY(bool inputEnabled READ inputEnabled WRITE setInputEnabled NOTIFY inputEnabledChanged FINAL)
    // GameController.Overlay.None, .Inventory, ... (QcScreenState::Overlay).
    Q_PROPERTY(QcScreenState::Overlay overlay READ overlay WRITE setOverlay NOTIFY overlayChanged FINAL)
    // How long a closed screen stays loaded (LazyScreen.keepAliveMs, decision E-691).
    Q_PROPERTY(int screenKeepAliveMs READ screenKeepAliveMs CONSTANT FINAL)
    // LazyScreen logs the time each screen takes to make (QTCRAFT_UI_STATS=1, decision E-690).
    Q_PROPERTY(bool logScreenCreation READ logScreenCreation CONSTANT FINAL)
    // Text the chat line starts with when it opens ("/" for the command key).
    Q_PROPERTY(QString chatStart READ chatStart NOTIFY overlayChanged FINAL)
    Q_PROPERTY(bool persistent READ persistent NOTIFY phaseChanged FINAL)
    Q_PROPERTY(QcSettingsController *settings READ settings CONSTANT FINAL)
    Q_PROPERTY(QcWorldLibraryController *library READ library CONSTANT FINAL)
    Q_PROPERTY(QcPlayerController *player READ player CONSTANT FINAL)
    Q_PROPERTY(QcTradeController *trade READ trade CONSTANT FINAL)
    // The editor of a sign (decision E-766).
    Q_PROPERTY(QcSignController *sign READ sign CONSTANT FINAL)
    Q_PROPERTY(QcHudState *hud READ hud CONSTANT FINAL)
    Q_PROPERTY(QcHandbookController *handbook READ handbook CONSTANT FINAL)
    Q_PROPERTY(QcGameInput *input READ input CONSTANT FINAL)
    // The pad on the screens: active input, focus ring, radial menu, on-screen keyboard (E-581 to E-584).
    Q_PROPERTY(QcGamepadUi *gamepad READ gamepadUi CONSTANT FINAL)
    // What the character previews draw (decision E-231).
    Q_PROPERTY(QcPreviewSource *preview READ preview CONSTANT FINAL)
    Q_PROPERTY(QcMultiplayerController *multiplayer READ multiplayer CONSTANT FINAL)
    // Photo mode: hidden HUD and screenshots (decision E-692).
    Q_PROPERTY(QcPhotoMode *photo READ photo CONSTANT FINAL)

public:
    // What the window shows (QcScreenState::Phase); QML compares against GameController.Phase.Playing etc.
    using Phase = QcScreenState::Phase;

    explicit QcGameController(QObject *parent = nullptr);
    ~QcGameController() override;

    QcWorldView *view() const { return m_parts.view; }
    void setView(QcWorldView *view);
    Phase phase() const { return m_screens.phase(); }
    QString statusText() const { return m_screens.statusText(); }
    bool inputEnabled() const { return m_screens.inputEnabled(); }
    void setInputEnabled(bool enabled) { m_screens.setInputEnabled(enabled); }
    // In-game screen on top of the world (QcScreenState::overlay).
    using Overlay = QcScreenState::Overlay;
    Overlay overlay() const { return m_screens.overlay(); }
    void setOverlay(Overlay overlay) { m_screens.setOverlay(overlay); }
    int screenKeepAliveMs() const { return m_screens.screenKeepAliveMs(); }
    bool logScreenCreation() const { return m_screens.logScreenCreation(); }
    QString chatStart() const { return m_screens.chatStart(); }
    // Opens the chat line with `start` already typed.
    void openChat(const QString &start) { m_screens.openChat(start); }

    bool persistent() const { return !m_worldDirectory.isEmpty(); }

    QcSettingsController *settings() const { return m_settings.get(); }
    QcWorldLibraryController *library() const { return m_library.get(); }
    QcPlayerController *player() const { return m_player.get(); }
    QcTradeController *trade() const { return m_trade.get(); }
    QcSignController *sign() const { return m_sign.get(); }
    QcHudState *hud() const { return m_hud.get(); }
    QcHandbookController *handbook() const { return m_handbook.get(); }
    QcGameInput *input() const { return m_input.get(); }
    QcGamepadUi *gamepadUi() { return &m_gamepadUi; }
    QcPreviewSource *preview() const { return m_preview.get(); }
    // Command line test hooks and the benchmark.
    QcStartupHooks *startup() const { return m_startup.get(); }
    // Joining and hosting network games (decision E-251).
    QcMultiplayerController *multiplayer() const { return m_multiplayer.get(); }
    QcPhotoMode *photo() const { return m_photo.get(); }
    // Plays the world of a network server: takes over a logged-in session (QcNetworkClient).
    void startRemoteSession(std::unique_ptr<QcClientSession> session);
    // The mods that went into the content of the running world.
    const QList<QcModStatus> &loadedMods() const { return m_worlds.loadedMods(); }

    // A world that is not saved (test hooks --play and --showcase), made by the generator type
    // `generator` (QcWorldGenerators).
    // `gameMode` is that of the player ("survival", "creative").
    Q_INVOKABLE void startSingleplayer(const QString &worldName, const QString &seedText, const QString &generator = QStringLiteral("terrain"),
                                       const QString &options = {}, const QString &gameMode = QStringLiteral("survival"));
    // Creates a saved world in the library and starts it. `setup` holds "worldType" (terrain, flat,
    // void, showcase; E-221, E-223), "layers" of a flat world (empty: the default), "keepInventory"
    // and "gameMode" ("survival", "creative"; decision E-282); missing keys keep the defaults.
    Q_INVOKABLE void createWorld(const QString &worldName, const QString &seedText, const QVariantMap &setup = {});
    // The layers a flat world gets by default (shown in the menu).
    Q_INVOKABLE QString defaultFlatLayers();
    Q_INVOKABLE void playWorld(const QString &folder);
    Q_INVOKABLE void saveWorld() const;
    Q_INVOKABLE void sendChat(const QString &text) const;
    Q_INVOKABLE void leaveGame();

    // Language used for content names (block and item labels).
    void setLanguage(const QString &language);
    void setIconProvider(QcItemIconProvider *provider) { m_icons.setProvider(provider); }
    // Opens the audio output (or stays silent when `enabled` is false, e.g. for screenshots) and
    // prepares the menu sounds from the base content.
    void startAudio(bool enabled);
    // Starts the gamepad backend (SDL3 when the build has it, decision E-540); without `enabled`
    // (screenshot runs, benchmarks, QTCRAFT_GAMEPAD=0) the game has no pads.
    void startGamepads(bool enabled);
    // Scripted runs with pad steps: a virtual pad instead of the backend (QcStartupHooks), taken by
    // the screens without the window's focus. The source must outlive the game or be replaced.
    void useScriptedGamepad(QcGamepadSource *source);
    // --stats / QTCRAFT_STATS: the debug data as a JSON line in the log every `intervalMs` while a
    // world runs (decision E-225); 0 leaves it off.
    void startStatsLog(int intervalMs);

signals:
    void viewChanged();
    void phaseChanged();
    void statusChanged();
    void inputEnabledChanged();
    void sessionStarted(QcClientSession *session);
    // An unsaved world (startSingleplayer) was not started: its options do not suit its type.
    void worldRefused(const QString &reason);
    void overlayChanged();

private:
    void setPhase(Phase phase, const QString &status = {}) { m_screens.setPhase(phase, status); }
    void attachWindow(QQuickWindow *window);
    // Ends the session and the server; returns the result of the final save.
    [[nodiscard]] QcStatus stopSession();
    // Name, seed, generator type and options of `world`; an empty directory runs it unsaved.
    void startWorld(const QString &directory, const QcWorldInfo &world);
    // The in-process server of `world` with base game and mods; false (and the error phase) when it
    // cannot start.
    [[nodiscard]] bool startServer(const QcWorldInfo &world);
    // Wires the new session (m_parts.session) to meshes, HUD, sounds and screens.
    void beginSession();
    // Enabling or disabling a mod changes the outfits on offer and the icons.
    void onContentReset();
    // The photo mode's keys, the hidden hand and the HUD shown again when a world ends.
    void connectPhotoMode();

    // The parts below refer to the ones above them (m_parts and m_icons first), so they are declared
    // in that order and die in the reverse one, before anything they refer to.
    QcSessionParts m_parts;
    QcScreenState m_screens;
    QcItemIcons m_icons;
    std::unique_ptr<QcAudioEngine> m_audio;
    std::unique_ptr<QcWorldLibraryController> m_library;
    std::unique_ptr<QcSettingsController> m_settings;
    QcWorldStarter m_worlds;
    std::unique_ptr<QcPlayerController> m_player;
    std::unique_ptr<QcTradeController> m_trade;
    std::unique_ptr<QcSignController> m_sign;
    std::unique_ptr<QcHudState> m_hud;
    std::unique_ptr<QcPhotoMode> m_photo;
    std::unique_ptr<QcHandbookController> m_handbook;
    std::unique_ptr<QcGameInput> m_input;
    std::unique_ptr<QcPreviewSource> m_preview;
    std::unique_ptr<QcBenchmarkRun> m_benchmark;
    std::unique_ptr<QcStartupHooks> m_startup;
    std::unique_ptr<QcMultiplayerController> m_multiplayer;
    std::unique_ptr<QcGamepadSource> m_gamepadSource;
    QcGamepadControls m_gamepad;
    QcGamepadUi m_gamepadUi;
    QcRenderBridge m_render;
    QcWorldAtlas m_worldAtlas;
    QcSessionWiring m_wiring;
    QcFrameDriver m_frame;
    QString m_worldDirectory;
};

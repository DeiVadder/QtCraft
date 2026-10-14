#pragma once

#include "QcClientSession.h"
#include "QcGameInput.h"
#include "QcHandbookController.h"
#include "QcHudState.h"
#include "QcItemIcons.h"
#include "QcModStatus.h"
#include "QcMultiplayerController.h"
#include "QcPlayerController.h"
#include "QcPreviewSource.h"
#include "QcRenderBridge.h"
#include "QcServerPersistence.h"
#include "QcResult.h"
#include "QcSessionParts.h"
#include "QcSettingsController.h"
#include "QcStartupHooks.h"
#include "QcWorldLibraryController.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

class QcAudioEngine;
class QcBenchmarkRun;
class QcBlockRenderTable;
class QcContentRegistry;
class QcItemIconProvider;
class QcStatsLog;
class QcTextureAtlas;
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

    Q_PROPERTY(QcWorldView *view READ view WRITE setView NOTIFY viewChanged FINAL)
    Q_PROPERTY(QString phase READ phase NOTIFY phaseChanged FINAL)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged FINAL)
    Q_PROPERTY(bool inputEnabled READ inputEnabled WRITE setInputEnabled NOTIFY inputEnabledChanged FINAL)
    Q_PROPERTY(QString overlay READ overlay WRITE setOverlay NOTIFY overlayChanged FINAL)
    // Text the chat line starts with when it opens ("/" for the command key).
    Q_PROPERTY(QString chatStart READ chatStart NOTIFY overlayChanged FINAL)
    Q_PROPERTY(bool persistent READ persistent NOTIFY phaseChanged FINAL)
    Q_PROPERTY(QcSettingsController *settings READ settings CONSTANT FINAL)
    Q_PROPERTY(QcWorldLibraryController *library READ library CONSTANT FINAL)
    Q_PROPERTY(QcPlayerController *player READ player CONSTANT FINAL)
    Q_PROPERTY(QcHudState *hud READ hud CONSTANT FINAL)
    Q_PROPERTY(QcHandbookController *handbook READ handbook CONSTANT FINAL)
    Q_PROPERTY(QcGameInput *input READ input CONSTANT FINAL)
    // What the character previews draw (decision E-231).
    Q_PROPERTY(QcPreviewSource *preview READ preview CONSTANT FINAL)
    Q_PROPERTY(QcMultiplayerController *multiplayer READ multiplayer CONSTANT FINAL)

public:
    explicit QcGameController(QObject *parent = nullptr);
    ~QcGameController() override;

    QcWorldView *view() const { return m_parts.view; }
    void setView(QcWorldView *view);
    // "menu", "loading", "playing", "error" or "disconnected" (the session ended, statusText says
    // why).
    QString phase() const { return m_phase; }
    QString statusText() const { return m_statusText; }
    bool inputEnabled() const { return m_inputEnabled; }
    void setInputEnabled(bool enabled);
    // In-game screen on top of the world: "" (none), "inventory", "furnace" (an open machine),
    // "chest", "pause", "settings", "controls" (settings at the key table), "debug" (settings at the
    // debug options), "texturepacks" (settings at the texture packs), "character" or "chat".
    QString overlay() const { return m_overlay; }
    void setOverlay(const QString &overlay);
    QString chatStart() const { return m_chatStart; }
    // Opens the chat line with `start` already typed.
    void openChat(const QString &start);
    bool persistent() const { return !m_worldDirectory.isEmpty(); }

    QcSettingsController *settings() const { return m_settings; }
    QcWorldLibraryController *library() const { return m_library; }
    QcPlayerController *player() const { return m_player; }
    QcHudState *hud() const { return m_hud; }
    QcHandbookController *handbook() const { return m_handbook; }
    QcGameInput *input() const { return m_input; }
    QcPreviewSource *preview() const { return m_preview; }
    // Command line test hooks and the benchmark.
    QcStartupHooks *startup() const { return m_startup; }
    // Joining and hosting network games (decision E-251).
    QcMultiplayerController *multiplayer() const { return m_multiplayer; }
    // Plays the world of a network server: takes over a logged-in session (QcNetworkClient).
    void startRemoteSession(QcClientSession *session);
    // The mods that went into the content of the running world.
    const QList<QcModStatus> &loadedMods() const { return m_loadedMods; }

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

public:
    // The translated message for a world that a damaged per-region file kept from opening (decision
    // E-366); empty when no such file was the reason.
    static QString damagedFileMessage(const QcServerPersistence::DamagedFile &damaged);

private:
    void setPhase(const QString &phase, const QString &status = {});
    void attachWindow(QQuickWindow *window);
    // Ends the session and the server; returns the result of the final save.
    QcStatus stopSession();
    // Name, seed, generator type and options of `world`; an empty directory runs it unsaved.
    void startWorld(const QString &directory, const QcWorldInfo &world);
    // Base game plus enabled mods for a new world; null (and the error phase) when it cannot load.
    std::shared_ptr<const QcContentRegistry> loadContent();
    bool startServer(const std::shared_ptr<const QcContentRegistry> &content, const QcWorldInfo &world);
    // Wires the new session (m_parts.session) to meshes, HUD, sounds and screens.
    void beginSession();
    void connectSessionWorld();
    void connectSessionPlayer();
    void onSessionState(QcClientSession::State state);
    void onContentReady();
    // Draws the world, the item icons and the character previews with `atlas`.
    void applyAtlas(const std::shared_ptr<const QcTextureAtlas> &atlas);
    void onMachineOpened();
    void onMachineClosed();
    void onHealthChanged();
    // Enabling or disabling a mod changes the outfits on offer and the icons.
    void onContentReset();
    void onFrame();
    // Seconds since the last frame (at most 0.1).
    float advanceFrameClocks();

    QcSessionParts m_parts;
    QcItemIcons m_icons;
    // The atlas the world is drawn with (decision E-301).
    std::shared_ptr<const QcTextureAtlas> m_atlas;
    QcAudioEngine *m_audio = nullptr;
    QcWorldLibraryController *m_library = nullptr;
    QcSettingsController *m_settings = nullptr;
    QcPlayerController *m_player = nullptr;
    QcHudState *m_hud = nullptr;
    QcHandbookController *m_handbook = nullptr;
    QcGameInput *m_input = nullptr;
    QcPreviewSource *m_preview = nullptr;
    QcBenchmarkRun *m_benchmark = nullptr;
    QcStartupHooks *m_startup = nullptr;
    QcMultiplayerController *m_multiplayer = nullptr;
    QcRenderBridge m_render;
    QString m_phase = QStringLiteral("menu");
    QString m_statusText;
    bool m_inputEnabled = true;
    QString m_overlay;
    QString m_chatStart;
    QString m_worldDirectory;
    std::shared_ptr<const QcBlockRenderTable> m_renderTable;
    QElapsedTimer m_sinceTick;
    QElapsedTimer m_frameClock;
    // The player's feet as drawn in the latest frame (interpolated between ticks).
    QcVec3 m_drawnFeet;
    QcStatsLog *m_statsLog = nullptr;
    QList<QcModStatus> m_loadedMods;
};

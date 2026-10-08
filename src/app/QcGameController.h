#pragma once

#include "QcCamera.h"
#include "QcGameSettings.h"

#include "QcClientSession.h"
#include "QcEntityMesher.h"
#include "QcInventoryModel.h"
#include "QcPlayerInput.h"
#include "QcRelativeMouse.h"
#include "QcWorldView.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <array>
#include <memory>
#include <optional>

class QcAudioEngine;
class QcBlockRenderTable;
class QcItemIconProvider;
class QcTextureAtlas;
class QcContentRegistry;
class QcLocalServerHost;
class QcMeshScheduler;
class QcScreenshotHook;
class QQuickWindow;

// Connects a client session to the world view and to keyboard/mouse input. Owns the in-process
// server of a single-player game. Lives in the GUI thread and is created from QML.
class QcGameController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(GameController)

    Q_PROPERTY(QcWorldView *view READ view WRITE setView NOTIFY viewChanged FINAL)
    Q_PROPERTY(QString phase READ phase NOTIFY phaseChanged FINAL)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString debugText READ debugText NOTIFY debugTextChanged FINAL)
    Q_PROPERTY(bool mouseCaptured READ mouseCaptured NOTIFY mouseCapturedChanged FINAL)
    Q_PROPERTY(bool inputEnabled READ inputEnabled WRITE setInputEnabled NOTIFY inputEnabledChanged FINAL)
    Q_PROPERTY(int viewDistance READ viewDistance WRITE setViewDistance NOTIFY viewDistanceChanged FINAL)
    Q_PROPERTY(QcInventoryModel *inventory READ inventory CONSTANT FINAL)
    Q_PROPERTY(double miningProgress READ miningProgress NOTIFY frameStateChanged FINAL)
    Q_PROPERTY(QString targetLabel READ targetLabel NOTIFY frameStateChanged FINAL)
    Q_PROPERTY(QStringList chatLines READ chatLines NOTIFY chatLinesChanged FINAL)
    Q_PROPERTY(QString overlay READ overlay WRITE setOverlay NOTIFY overlayChanged FINAL)
    Q_PROPERTY(QVariantList worlds READ worlds NOTIFY worldsChanged FINAL)
    Q_PROPERTY(bool persistent READ persistent NOTIFY phaseChanged FINAL)
    Q_PROPERTY(double health READ health NOTIFY healthChanged FINAL)
    Q_PROPERTY(double maxHealth READ maxHealth NOTIFY healthChanged FINAL)
    Q_PROPERTY(bool dead READ dead NOTIFY healthChanged FINAL)
    Q_PROPERTY(int food READ food NOTIFY foodChanged FINAL)
    Q_PROPERTY(int maxFood READ maxFood NOTIFY foodChanged FINAL)
    // The open machine (furnace screen): its name, the share of fuel left and of the item done.
    Q_PROPERTY(QString machineLabel READ machineLabel NOTIFY machineChanged FINAL)
    Q_PROPERTY(double machineBurn READ machineBurn NOTIFY machineChanged FINAL)
    Q_PROPERTY(int containerSlots READ containerSlots NOTIFY machineChanged FINAL)
    Q_PROPERTY(double machineProgress READ machineProgress NOTIFY machineChanged FINAL)
    Q_PROPERTY(QString timeOfDay READ timeOfDay NOTIFY frameStateChanged FINAL)
    Q_PROPERTY(int craftingSize READ craftingSize NOTIFY craftingChanged FINAL)
    Q_PROPERTY(QVariantList craftableRecipes READ craftableRecipes NOTIFY craftingChanged FINAL)
    Q_PROPERTY(QVariantList mods READ mods NOTIFY modsChanged FINAL)
    Q_PROPERTY(double fieldOfView READ fieldOfView WRITE setFieldOfView NOTIFY settingsChanged FINAL)
    Q_PROPERTY(double mouseSensitivity READ mouseSensitivity WRITE setMouseSensitivity NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool invertMouse READ invertMouse WRITE setInvertMouse NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool showDebug READ showDebug WRITE setShowDebug NOTIFY settingsChanged FINAL)
    // "" follows the system language; otherwise a locale name such as "de".
    Q_PROPERTY(QString language READ language WRITE requestLanguage NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QString modsDirectory READ modsDirectory NOTIFY modsChanged FINAL)
    Q_PROPERTY(QString cameraMode READ cameraMode WRITE setCameraMode NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QVariantMap character READ character WRITE setCharacter NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QVariantList outfits READ outfits NOTIFY appearancesChanged FINAL)
    Q_PROPERTY(QVariantList hairStyles READ hairStyles NOTIFY appearancesChanged FINAL)
    // Volumes in 0..1 and the mute switch (settings).
    Q_PROPERTY(double masterVolume READ masterVolume WRITE setMasterVolume NOTIFY settingsChanged FINAL)
    Q_PROPERTY(double effectsVolume READ effectsVolume WRITE setEffectsVolume NOTIFY settingsChanged FINAL)
    Q_PROPERTY(double musicVolume READ musicVolume WRITE setMusicVolume NOTIFY settingsChanged FINAL)
    Q_PROPERTY(double ambientVolume READ ambientVolume WRITE setAmbientVolume NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY settingsChanged FINAL)

public:
    static constexpr int defaultViewDistance = QcGameSettings::defaultViewDistance;
    static constexpr double defaultMouseSensitivity = QcGameSettings::defaultMouseSensitivity;
    static constexpr float keyboardLookSpeed = 120.0f;

    explicit QcGameController(QObject *parent = nullptr);
    ~QcGameController() override;

    QcWorldView *view() const { return m_view; }
    void setView(QcWorldView *view);
    // "menu", "loading", "playing" or "error".
    QString phase() const { return m_phase; }
    QString statusText() const { return m_statusText; }
    QString debugText() const { return m_debugText; }
    bool mouseCaptured() const { return m_mouseCaptured; }
    bool inputEnabled() const { return m_inputEnabled; }
    void setInputEnabled(bool enabled);
    int viewDistance() const { return m_viewDistance; }
    void setViewDistance(int chunks);

    // Settings file below the data root (QTCRAFT_DATA_DIR in tests): view distance, field of view,
    // mouse, debug overlay and language.
    static QString settingsPath();
    // Language stored in the settings ("" = system).
    static QString storedLanguage();
    void loadSettings();
    Q_INVOKABLE void saveSettings();
    // View distance from the command line: used now, but not written to the settings.
    void overrideViewDistance(int chunks);
    double fieldOfView() const { return m_fieldOfView; }
    void setFieldOfView(double degrees);
    double mouseSensitivity() const { return m_mouseSensitivity; }
    void setMouseSensitivity(double sensitivity);
    bool invertMouse() const { return m_invertMouse; }
    void setInvertMouse(bool invert);
    bool showDebug() const { return m_showDebug; }
    void setShowDebug(bool show);
    QString language() const { return m_languageSetting; }
    // Stores the choice and asks the application to switch the UI language now.
    void requestLanguage(const QString &requested);
    // "first", "behind" or "front"; F5 cycles through them.
    QString cameraMode() const;
    void setCameraMode(const QString &mode);
    // The player's look as {outfit, hair, skin, hairColor, shirt, pants} with "#rrggbb" colours.
    QVariantMap character() const;
    void setCharacter(const QVariantMap &character);
    // Outfits and hair styles of the base game as [{name, label}].
    QVariantList outfits() const;
    QVariantList hairStyles() const;
    double masterVolume() const { return m_masterVolume; }
    void setMasterVolume(double volume);
    double effectsVolume() const { return m_effectsVolume; }
    void setEffectsVolume(double volume);
    double musicVolume() const { return m_musicVolume; }
    void setMusicVolume(double volume);
    double ambientVolume() const { return m_ambientVolume; }
    void setAmbientVolume(double volume);
    bool muted() const { return m_muted; }
    void setMuted(bool muted);
    // Opens the audio output (or stays silent when `enabled` is false, e.g. for screenshots) and
    // prepares the menu sounds from the base content.
    void startAudio(bool enabled);
    // Test hook: freezes the local player's pose ("walk", "swing", "swim", "jump", "sneak", "death")
    // for screenshots.
    void setForcedPose(const QString &pose) { m_forcedPose = pose; }

    QcInventoryModel *inventory() const { return m_inventory; }
    double miningProgress() const { return m_miningProgress; }
    QString targetLabel() const { return m_targetLabel; }
    QStringList chatLines() const { return m_chatLines; }
    // In-game screen on top of the world: "" (none), "inventory", "furnace" (an open machine),
    // "pause" or "chat".
    QString overlay() const { return m_overlay; }
    void setOverlay(const QString &overlay);
    QVariantList worlds() const { return m_worlds; }
    QVariantList mods() const { return m_mods; }
    QString modsDirectory() const { return m_modsDirectory; }
    // Where mods are looked for; empty loads only the base game (test hook --no-mods).
    void setModsDirectory(const QString &directory);
    bool persistent() const { return !m_worldDirectory.isEmpty(); }
    // Language used for content names (block and item labels).
    void setLanguage(const QString &language);
    void setIconProvider(QcItemIconProvider *provider) { m_iconProvider = provider; }

    // A world that is not saved (test hook --play).
    Q_INVOKABLE void startSingleplayer(const QString &worldName, const QString &seedText);
    Q_INVOKABLE void refreshWorlds();
    // Creates a saved world in the library and starts it.
    Q_INVOKABLE void createWorld(const QString &worldName, const QString &seedText, bool keepInventory = true);
    Q_INVOKABLE void playWorld(const QString &folder);
    // Deletes a saved world permanently; the UI asks for confirmation first.
    Q_INVOKABLE void deleteWorld(const QString &folder);
    Q_INVOKABLE void saveWorld();
    // Mod list as found on disk: id, name, version, description, state (loaded/disabled/failed), reason.
    Q_INVOKABLE void refreshMods();
    // Takes effect when the next world starts.
    Q_INVOKABLE void setModEnabled(const QString &id, bool enabled);
    Q_INVOKABLE void openModsFolder();
    Q_INVOKABLE void selectHotbarSlot(int slot);
    // Rows of QcInventoryModel; count 0 moves the whole stack.
    Q_INVOKABLE void moveItem(int from, int to, int count = 0);
    Q_INVOKABLE void takeCraftingResult();
    Q_INVOKABLE void craftRecipe(const QString &recipe);
    int craftingSize() const { return m_session ? m_session->craftingSize() : 2; }
    QVariantList craftableRecipes() const { return m_craftableRecipes; }
    Q_INVOKABLE void sendChat(const QString &text);
    Q_INVOKABLE void respawn();
    double health() const { return m_session ? m_session->health() : 0.0; }
    double maxHealth() const { return m_session ? m_session->maxHealth() : 20.0; }
    bool dead() const { return m_session && m_session->isDead(); }
    int food() const { return m_session ? m_session->food() : QcItemDefinition::maxFood; }
    int maxFood() const { return m_session ? m_session->maxFood() : QcItemDefinition::maxFood; }
    QString machineLabel() const { return m_machineLabel; }
    double machineBurn() const { return m_session ? m_session->machineBurn() : 0.0; }
    // Slots of the open chest, 0 when none is open.
    int containerSlots() const { return m_session && m_session->containerOpen() ? int(m_session->machineSlots().size()) : 0; }
    double machineProgress() const { return m_session ? m_session->machineProgress() : 0.0; }
    QString timeOfDay() const { return m_timeOfDay; }
    Q_INVOKABLE void leaveGame();
    Q_INVOKABLE void captureMouse();
    Q_INVOKABLE void releaseMouse();

    // Test hooks (command line): initial view direction and the screenshot to take once loaded.
    void setInitialLook(float yaw, float pitch);
    void setSpawnColumn(int x, int z) { m_spawnColumn = std::array<int, 2>{x, z}; }
    void setScreenshotHook(QcScreenshotHook *hook);
    // Test hook: chat lines sent and overlay opened once the world is playable.
    void setStartupScript(QStringList chatLines, QString overlay);
    // Test hook (QTCRAFT_BENCHMARK=<seconds>): once playing and after a short warm-up, frame and
    // server tick times are recorded, then the result is logged and the application quits.
    // Benchmark scenarios: "sprint" walks ahead, "stand" stands still, "light" stands while torches
    // are placed and removed around the player (block light updates and remeshing).
    void setBenchmark(int seconds, const QString &scenario)
    {
        m_benchmarkSeconds = seconds;
        m_benchmarkWalk = scenario != u"stand" && scenario != u"light";
        m_benchmarkLight = scenario == u"light";
    }
    QcClientSession *session() const { return m_session; }

    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void viewChanged();
    void phaseChanged();
    void statusChanged();
    void debugTextChanged();
    void mouseCapturedChanged();
    void inputEnabledChanged();
    void viewDistanceChanged();
    void sessionStarted(QcClientSession *session);
    void frameStateChanged();
    void chatLinesChanged();
    void overlayChanged();
    void worldsChanged();
    void modsChanged();
    void settingsChanged();
    // The character screen's outfit and hair lists (content or language changed).
    void appearancesChanged();
    // The application installs the translator for `language` ("" = system) and retranslates.
    void languageChangeRequested(const QString &language);
    void healthChanged();
    void foodChanged();
    void machineChanged();
    void craftingChanged();

private:
    void setPhase(const QString &phase, const QString &status = {});
    void attachWindow(QQuickWindow *window);
    void onContentReady();
    void onFrame();
    void updateIntentFromKeys(float frameSeconds);
    // Ends the session and the server; returns the result of the final save.
    QcStatus stopSession();
    void startWorld(const QString &directory, const QString &worldName, qint64 seed);
    void handleMouseButton(Qt::MouseButton button, bool pressed);
    void updateIcons(const QcTextureAtlas &atlas);
    void updateEntities(double alpha, QcCameraMode mode);
    void updateHand(double alpha, const QcVec3 &eye);
    QcEntityMesher::ModelLook playerLook(const QcAppearance &appearance) const;
    QcEntityPose localPose(double alpha) const;
    std::optional<QcEntityMesher::ItemLook> heldLook(QcItemId item) const;
    // Base game plus enabled mods, loaded on demand for the character screen's lists.
    const QcContentRegistry &characterContent() const;
    // Enabling or disabling a mod changes the outfits on offer.
    void resetCharacterContent();
    QVariantList appearanceList(QcAppearanceDefinition::Kind kind) const;
    // Block light (0..1) of entities and the hand at a position, as the meshes show it.
    float blockLightAt(QcBlockPos pos) const;
    // The state a block item is drawn in (icons, hand, dropped items).
    static QcBlockId itemState(const QcContentRegistry &content, QcBlockId block);
    static QcEntityMesher::ItemLook itemLook(const QcContentRegistry &content, QcItemId item);
    void updateSky(double alpha);
    void updateInventory();
    void applyVolumes();
    // Plays the click sound when the mouse is released over a button of the UI.
    void clickIfButton(QPointF scenePosition);

    QcRelativeMouse m_relativeMouse;
    QPointer<QcWorldView> m_view;
    QPointer<QQuickWindow> m_window;
    QString m_phase = QStringLiteral("menu");
    QString m_statusText;
    QString m_debugText;
    bool m_mouseCaptured = false;
    bool m_inputEnabled = true;
    int m_viewDistance = defaultViewDistance;
    double m_fieldOfView = QcCamera::defaultFieldOfView;
    QcCameraMode m_cameraMode = QcCameraMode::FirstPerson;
    QcAppearance m_appearance;
    QString m_forcedPose;
    mutable std::shared_ptr<const QcContentRegistry> m_characterContent;
    double m_mouseSensitivity = defaultMouseSensitivity;
    double m_masterVolume = QcGameSettings::defaultMasterVolume;
    double m_effectsVolume = QcGameSettings::defaultEffectsVolume;
    double m_musicVolume = QcGameSettings::defaultMusicVolume;
    double m_ambientVolume = QcGameSettings::defaultAmbientVolume;
    bool m_muted = false;
    QcAudioEngine *m_audio = nullptr;
    bool m_invertMouse = false;
    bool m_showDebug = true;
    QString m_languageSetting;
    QcGameSettings m_storedSettings;
    int m_commandLineViewDistance = 0;
    int m_benchmarkSeconds = 0;
    bool m_benchmarkRunning = false;
    bool m_benchmarkWalk = true;
    bool m_benchmarkLight = false;
    QElapsedTimer m_benchmarkFrame;
    QcVec3 m_benchmarkCheckpoint;
    QList<qint64> m_benchmarkFrameNs;
    void startBenchmark();
    void finishBenchmark();

    std::unique_ptr<QcLocalServerHost> m_server;
    QcClientSession *m_session = nullptr;
    QcMeshScheduler *m_meshes = nullptr;
    std::shared_ptr<const QcBlockRenderTable> m_renderTable;

    QSet<int> m_keysDown;
    QElapsedTimer m_sinceTick;
    QElapsedTimer m_frameClock;
    QElapsedTimer m_fpsClock;
    int m_framesSinceFps = 0;
    double m_fps = 0.0;
    float m_initialYaw = 0.0f;
    float m_initialPitch = 0.0f;
    std::optional<std::array<int, 2>> m_spawnColumn;
    QcInventoryModel *m_inventory = nullptr;
    QcItemIconProvider *m_iconProvider = nullptr;
    QString m_language = QStringLiteral("en");
    double m_miningProgress = 0.0;
    QString m_targetLabel;
    QStringList m_chatLines;
    QString m_overlay;
    QVariantList m_worlds;
    QVariantList m_mods;
    // Mods that passed discovery but failed when the last world loaded its content, with the reason.
    QHash<QString, QString> m_modLoadFailures;
    QString m_modsDirectory;
    QString m_worldDirectory;
    std::shared_ptr<const QcTextureAtlas> m_atlas;
    QString m_timeOfDay;
    QVariantList m_craftableRecipes;
    QStringList m_startupChat;
    QString m_startupOverlay;
    QString m_machineLabel;
    // Test hook --overlay furnace: the screenshot waits until the machine has opened and filled.
    bool m_fillMachineOnOpen = false;
    QPointer<QcScreenshotHook> m_screenshotHook;
    bool m_screenshotReleased = false;
};

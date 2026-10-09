#include "QcGameController.h"

#include "QcBlockIcon.h"
#include "QcLightVolume.h"

#include "QcAudioEngine.h"
#include "QcSoundKeys.h"

#include "QcModLoader.h"
#include "QcMouseLook.h"
#include "QcBlockRenderTable.h"
#include "QcClientSession.h"
#include "QcLocalServerHost.h"
#include "QcMeshScheduler.h"
#include "QcPhysics.h"
#include "QcScreenshotHook.h"
#include "QcTerrainGenerator.h"
#include "QcTextureAtlas.h"
#include "QcTransport.h"
#include "QcWorldView.h"
#include "QcCrafting.h"
#include "QcDayCycle.h"
#include "QcEntityMesher.h"
#include "QcGameSettings.h"
#include "QcItemIconProvider.h"
#include "QcWorldLibrary.h"

#include <QCursor>
#include <QQuickItem>
#include <QDesktopServices>
#include <QDir>
#include <QUrl>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QLocale>
#include <QVariantMap>
#include <QLoggingCategory>
#include <QQuickWindow>
#include <QCoreApplication>
#include <QTimer>
#include <QSettings>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <functional>

#if defined(Q_OS_UNIX)
#include <sys/resource.h>
#endif

using namespace Qt::StringLiterals;

namespace {

// Highest resident memory of the process so far; 0 where the platform does not report it.
double peakMemoryMegabytes()
{
#if defined(Q_OS_UNIX)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0.0;
    }
#if defined(Q_OS_DARWIN)
    return double(usage.ru_maxrss) / (1024.0 * 1024.0); // bytes
#else
    return double(usage.ru_maxrss) / 1024.0; // kilobytes
#endif
#else
    return 0.0;
#endif
}

Q_LOGGING_CATEGORY(lcGame, "qtcraft.game")

// Stable across runs and platforms, unlike qHash.
qint64 seedFromText(const QString &text)
{
    bool ok = false;
    const qint64 number = text.trimmed().toLongLong(&ok);
    if (ok) {
        return number;
    }
    quint64 hash = 14695981039346656037ull;
    for (const QChar c : text) {
        hash ^= c.unicode();
        hash *= 1099511628211ull;
    }
    return qint64(hash);
}

QcVec3 lerp(const QcVec3 &a, const QcVec3 &b, double t)
{
    return a + (b - a) * t;
}

} // namespace

QcGameController::QcGameController(QObject *parent)
    : QObject(parent)
    , m_audio(new QcAudioEngine(this))
    , m_inventory(new QcInventoryModel(this))
{
    m_frameClock.start();
    m_fpsClock.start();
}

QcGameController::~QcGameController()
{
    if (const QcStatus saved = stopSession(); !saved) {
        qCWarning(lcGame) << "Saving the world failed:" << saved.error();
    }
}

void QcGameController::setView(QcWorldView *view)
{
    if (m_view == view) {
        return;
    }
    m_view = view;
    if (m_view) {
        if (m_view->window()) {
            attachWindow(m_view->window());
        }
        connect(m_view, &QQuickItem::windowChanged, this, &QcGameController::attachWindow);
    }
    emit viewChanged();
}

void QcGameController::attachWindow(QQuickWindow *window)
{
    if (m_window == window) {
        return;
    }
    if (m_window) {
        m_window->removeEventFilter(this);
        disconnect(m_window, nullptr, this, nullptr);
    }
    m_window = window;
    if (m_window) {
        m_window->installEventFilter(this);
        connect(m_window, &QQuickWindow::afterAnimating, this, &QcGameController::onFrame);
        connect(m_window, &QWindow::activeChanged, this, [this] {
            if (!m_window->isActive()) {
                m_keysDown.clear();
                releaseMouse();
            }
        });
    }
}

void QcGameController::setPhase(const QString &phase, const QString &status)
{
    if (m_statusText != status) {
        m_statusText = status;
        emit statusChanged();
    }
    if (m_phase != phase) {
        m_phase = phase;
        emit phaseChanged();
    }
}

void QcGameController::setInputEnabled(bool enabled)
{
    if (m_inputEnabled == enabled) {
        return;
    }
    m_inputEnabled = enabled;
    if (!enabled) {
        m_keysDown.clear();
        releaseMouse();
    }
    emit inputEnabledChanged();
}

void QcGameController::setViewDistance(int chunks)
{
    chunks = QcGameSettings::clampViewDistance(chunks);
    if (m_viewDistance == chunks) {
        return;
    }
    m_viewDistance = chunks;
    emit viewDistanceChanged();
}

QString QcGameController::settingsPath()
{
    return QDir(QcWorldLibrary::dataRoot()).filePath(u"settings.ini"_s);
}

QString QcGameController::storedLanguage()
{
    return QcGameSettings::load(settingsPath()).language;
}

void QcGameController::loadSettings()
{
    m_storedSettings = QcGameSettings::load(settingsPath());
    setViewDistance(m_storedSettings.viewDistance);
    setFieldOfView(m_storedSettings.fieldOfView);
    setMouseSensitivity(m_storedSettings.mouseSensitivity);
    setInvertMouse(m_storedSettings.invertMouse);
    setShowDebug(m_storedSettings.showDebug);
    m_languageSetting = m_storedSettings.language;
    m_cameraMode = m_storedSettings.cameraMode;
    m_appearance = m_storedSettings.appearance;
    m_masterVolume = m_storedSettings.masterVolume;
    m_effectsVolume = m_storedSettings.effectsVolume;
    m_musicVolume = m_storedSettings.musicVolume;
    m_ambientVolume = m_storedSettings.ambientVolume;
    m_muted = m_storedSettings.muted;
    applyVolumes();
    emit settingsChanged();
}

void QcGameController::applyVolumes()
{
    m_audio->setVolumes(m_masterVolume, m_effectsVolume, m_musicVolume, m_ambientVolume, m_muted);
}

void QcGameController::setMasterVolume(double volume)
{
    volume = QcGameSettings::clampVolume(volume, m_masterVolume);
    if (m_masterVolume != volume) {
        m_masterVolume = volume;
        applyVolumes();
        emit settingsChanged();
    }
}

void QcGameController::setEffectsVolume(double volume)
{
    volume = QcGameSettings::clampVolume(volume, m_effectsVolume);
    if (m_effectsVolume != volume) {
        m_effectsVolume = volume;
        applyVolumes();
        emit settingsChanged();
    }
}

void QcGameController::setMusicVolume(double volume)
{
    volume = QcGameSettings::clampVolume(volume, m_musicVolume);
    if (m_musicVolume != volume) {
        m_musicVolume = volume;
        applyVolumes();
        emit settingsChanged();
    }
}

void QcGameController::setAmbientVolume(double volume)
{
    volume = QcGameSettings::clampVolume(volume, m_ambientVolume);
    if (m_ambientVolume != volume) {
        m_ambientVolume = volume;
        applyVolumes();
        emit settingsChanged();
    }
}

void QcGameController::setMuted(bool muted)
{
    if (m_muted != muted) {
        m_muted = muted;
        applyVolumes();
        emit settingsChanged();
    }
}

void QcGameController::startAudio(bool enabled)
{
    m_audio->start(enabled);
    applyVolumes();
    if (m_audio->isActive() && !m_session) {
        // Menu clicks come from the content as well; the base game and mods are loaded once here.
        characterContent();
        m_audio->setContent(m_characterContent);
    }
}

void QcGameController::clickIfButton(QPointF scenePosition)
{
    if (!m_window || !m_audio->isActive()) {
        return;
    }
    // The topmost visible item under the mouse, children in front of their parents and higher z first.
    const std::function<QQuickItem *(QQuickItem *)> topmost = [&](QQuickItem *item) -> QQuickItem * {
        if (!item->isVisible() || !item->isEnabled()) {
            return nullptr;
        }
        QList<QQuickItem *> children = item->childItems();
        std::ranges::stable_sort(children, [](const QQuickItem *a, const QQuickItem *b) { return a->z() > b->z(); });
        for (QQuickItem *child : std::as_const(children)) {
            if (QQuickItem *hit = topmost(child)) {
                return hit;
            }
        }
        return item->contains(item->mapFromScene(scenePosition)) ? item : nullptr;
    };
    for (const QQuickItem *item = topmost(m_window->contentItem()); item; item = item->parentItem()) {
        if (item->inherits("QQuickAbstractButton")) {
            m_audio->playUi(QString::fromLatin1(QcSoundKeys::uiClick));
            return;
        }
    }
}

void QcGameController::overrideViewDistance(int chunks)
{
    setViewDistance(chunks);
    m_commandLineViewDistance = m_viewDistance;
}

void QcGameController::saveSettings()
{
    QcGameSettings settings;
    // A view distance given on the command line is not stored unless the player changed it since.
    settings.viewDistance = m_commandLineViewDistance == m_viewDistance ? m_storedSettings.viewDistance : m_viewDistance;
    settings.fieldOfView = m_fieldOfView;
    settings.mouseSensitivity = m_mouseSensitivity;
    settings.invertMouse = m_invertMouse;
    settings.showDebug = m_showDebug;
    settings.language = m_languageSetting;
    settings.cameraMode = m_cameraMode;
    settings.appearance = m_appearance;
    settings.masterVolume = m_masterVolume;
    settings.effectsVolume = m_effectsVolume;
    settings.musicVolume = m_musicVolume;
    settings.ambientVolume = m_ambientVolume;
    settings.muted = m_muted;
    if (const QcStatus saved = settings.save(settingsPath()); !saved) {
        qCWarning(lcGame) << saved.error();
        return;
    }
    m_storedSettings = settings;
}

void QcGameController::setFieldOfView(double degrees)
{
    degrees = QcGameSettings::clampFieldOfView(degrees);
    if (m_fieldOfView != degrees) {
        m_fieldOfView = degrees;
        emit settingsChanged();
    }
}

void QcGameController::setMouseSensitivity(double sensitivity)
{
    sensitivity = QcGameSettings::clampMouseSensitivity(sensitivity);
    if (m_mouseSensitivity != sensitivity) {
        m_mouseSensitivity = sensitivity;
        emit settingsChanged();
    }
}

void QcGameController::setInvertMouse(bool invert)
{
    if (m_invertMouse != invert) {
        m_invertMouse = invert;
        emit settingsChanged();
    }
}

void QcGameController::setShowDebug(bool show)
{
    if (m_showDebug != show) {
        m_showDebug = show;
        emit settingsChanged();
    }
}

void QcGameController::setLanguage(const QString &language)
{
    m_language = language;
    // Item and recipe names come from the content's translations.
    if (m_session && m_session->content()) {
        m_inventory->setContent(m_session->content(), m_language);
        updateInventory();
    }
    emit appearancesChanged();
}

void QcGameController::requestLanguage(const QString &requested)
{
    const QString language = QcGameSettings::cleanLanguage(requested);
    if (m_languageSetting == language) {
        return;
    }
    m_languageSetting = language;
    emit settingsChanged();
    emit languageChangeRequested(language);
}

void QcGameController::setInitialLook(float yaw, float pitch)
{
    m_initialYaw = yaw;
    m_initialPitch = std::clamp(pitch, -90.0f, 90.0f);
}

void QcGameController::setStartupScript(QStringList chatLines, QString overlay)
{
    m_startupChat = std::move(chatLines);
    m_startupOverlay = std::move(overlay);
}

void QcGameController::setScreenshotHook(QcScreenshotHook *hook)
{
    m_screenshotHook = hook;
    m_screenshotReleased = false;
    if (hook) {
        hook->hold();
    }
}

void QcGameController::startSingleplayer(const QString &worldName, const QString &seedText)
{
    startWorld({}, worldName.trimmed().isEmpty() ? tr("New World") : worldName.trimmed(), seedFromText(seedText));
}

void QcGameController::refreshWorlds()
{
    m_worlds.clear();
    const QcWorldLibrary library(QcWorldLibrary::defaultRoot());
    const QList<QcWorldLibrary::Entry> entries = library.list();
    for (const QcWorldLibrary::Entry &entry : entries) {
        m_worlds.append(QVariantMap{{u"folder"_s, entry.folder},
                                    {u"name"_s, entry.info.name},
                                    {u"seed"_s, QString::number(entry.info.seed)},
                                    {u"lastPlayed"_s, QLocale().toString(entry.info.lastPlayed.toLocalTime(), QLocale::ShortFormat)},
                                    {u"error"_s, entry.error}});
    }
    emit worldsChanged();
}

void QcGameController::setModsDirectory(const QString &directory)
{
    m_modsDirectory = directory;
    refreshMods();
    resetCharacterContent();
}

void QcGameController::refreshMods()
{
    m_mods.clear();
    const QList<QcModStatus> mods = QcModLoader::discover(m_modsDirectory);
    for (const QcModStatus &mod : mods) {
        QString state = u"loaded"_s;
        QString reason = mod.reason;
        if (mod.state == QcModStatus::State::Disabled) {
            state = u"disabled"_s;
        } else if (mod.state == QcModStatus::State::Failed) {
            state = u"failed"_s;
        } else if (m_modLoadFailures.contains(mod.info.id)) {
            // Found fine on disk, but its content was refused when the last world started.
            state = u"failed"_s;
            reason = m_modLoadFailures.value(mod.info.id);
        }
        m_mods.append(QVariantMap{{u"id"_s, mod.info.id},
                                  {u"name"_s, mod.info.name.isEmpty() ? QDir(mod.info.directory).dirName() : mod.info.name},
                                  {u"version"_s, mod.info.version},
                                  {u"description"_s, mod.info.description},
                                  {u"state"_s, state},
                                  {u"reason"_s, reason}});
    }
    emit modsChanged();
}

void QcGameController::setModEnabled(const QString &id, bool enabled)
{
    if (const QcStatus written = QcModLoader::setModEnabled(m_modsDirectory, id, enabled); !written) {
        qCWarning(lcGame) << "Cannot change the mod settings:" << written.error();
    }
    refreshMods();
    resetCharacterContent();
}

void QcGameController::openModsFolder()
{
    if (!m_modsDirectory.isEmpty() && QDir().mkpath(m_modsDirectory)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_modsDirectory));
    }
}

void QcGameController::createWorld(const QString &worldName, const QString &seedText, bool keepInventory)
{
    const QcWorldLibrary library(QcWorldLibrary::defaultRoot());
    const QString name = worldName.trimmed().isEmpty() ? tr("New World") : worldName.trimmed();
    const QcResult<QcWorldLibrary::Entry> entry = library.create(name, seedFromText(seedText), keepInventory);
    if (!entry) {
        setPhase(u"error"_s, tr("Cannot create the world: %1").arg(entry.error()));
        return;
    }
    refreshWorlds();
    startWorld(entry->directory, entry->info.name, entry->info.seed);
}

void QcGameController::playWorld(const QString &folder)
{
    const QcWorldLibrary library(QcWorldLibrary::defaultRoot());
    const QList<QcWorldLibrary::Entry> entries = library.list();
    const auto it = std::ranges::find(entries, folder, &QcWorldLibrary::Entry::folder);
    if (it == entries.end() || !it->error.isEmpty()) {
        setPhase(u"error"_s, it == entries.end() ? tr("World not found") : tr("Cannot load the world: %1").arg(it->error));
        return;
    }
    startWorld(it->directory, it->info.name, it->info.seed);
}

void QcGameController::deleteWorld(const QString &folder)
{
    const QcStatus removed = QcWorldLibrary(QcWorldLibrary::defaultRoot()).remove(folder);
    if (!removed) {
        qCWarning(lcGame) << removed.error();
    }
    refreshWorlds();
}

void QcGameController::saveWorld()
{
    sendChat(u"/save"_s);
}

void QcGameController::selectHotbarSlot(int slot)
{
    if (m_session) {
        m_session->selectHotbarSlot(slot);
    }
}

void QcGameController::moveItem(int from, int to, int count)
{
    const int fromSlot = QcInventoryModel::slotOfRow(from);
    const int toSlot = QcInventoryModel::slotOfRow(to);
    if (m_session && fromSlot >= 0 && toSlot >= 0) {
        m_session->moveInventoryItem(fromSlot, toSlot, count);
    }
}

void QcGameController::takeCraftingResult()
{
    if (m_session) {
        m_session->takeCraftingResult();
    }
}

void QcGameController::craftRecipe(const QString &recipe)
{
    if (m_session) {
        m_session->craftRecipe(recipe);
    }
}

void QcGameController::updateInventory()
{
    m_inventory->setInventory(m_session->inventory(), m_session->craftingGrid(), m_session->craftingResult(),
                              m_session->machineOpen() ? m_session->machineSlots() : QList<QcItemStack>());
    // Recipes the inventory can make right now, for the recipe list next to the grid.
    m_craftableRecipes.clear();
    const auto content = m_session->content();
    if (content) {
        for (const QcRecipeDefinition &recipe : content->recipes()) {
            if (QcCrafting::gridSizeNeeded(recipe) > m_session->craftingSize()) {
                continue;
            }
            const int possible = QcCrafting::timesCraftable(*content, m_session->inventory(), recipe, 64);
            const std::optional<QcItemId> result = content->itemId(recipe.resultItem);
            if (possible <= 0 || !result) {
                continue;
            }
            const QcItemDefinition &definition = content->item(*result);
            m_craftableRecipes.append(QVariantMap{{u"recipe"_s, recipe.name},
                                                  {u"label"_s, content->translate(definition.translationKey, m_language)},
                                                  {u"icon"_s, QString(u"image://items/"_s + definition.name)},
                                                  {u"count"_s, recipe.resultCount},
                                                  {u"possible"_s, possible}});
        }
    }
    emit craftingChanged();
}

void QcGameController::respawn()
{
    if (m_session) {
        m_session->respawn();
    }
}

void QcGameController::sendChat(const QString &text)
{
    if (m_session) {
        m_session->sendChat(text);
    }
}

void QcGameController::setOverlay(const QString &overlay)
{
    if (m_overlay == overlay) {
        return;
    }
    m_overlay = overlay;
    m_keysDown.clear();
    if (m_session) {
        m_session->setMining(false);
        // Leaving the inventory hands back the grid contents (and closes a crafting table); leaving
        // the furnace or chest screen closes the block.
        if (m_overlay != u"inventory"_s) {
            m_session->closeCrafting();
        }
        if (m_overlay != u"furnace"_s && m_overlay != u"chest"_s) {
            m_session->closeMachine();
        }
    }
    if (!overlay.isEmpty()) {
        releaseMouse();
    }
    emit overlayChanged();
}

void QcGameController::startWorld(const QString &directory, const QString &worldName, qint64 seed)
{
    if (const QcStatus saved = stopSession(); !saved) {
        qCWarning(lcGame) << "Saving the previous world failed:" << saved.error();
    }
    m_worldDirectory = directory;
    setOverlay({});
    setPhase(u"loading"_s, tr("Loading content..."));

    QStringList warnings;
    QcResult<QcModLoader::Result> content = QcModLoader::load(m_modsDirectory, warnings);
    for (const QString &warning : std::as_const(warnings)) {
        qCWarning(lcGame) << "Content:" << warning;
    }
    if (!content) {
        setPhase(u"error"_s, tr("Cannot load game content: %1").arg(content.error()));
        return;
    }
    m_modLoadFailures.clear();
    for (const QcModStatus &mod : std::as_const(content->mods)) {
        if (mod.state == QcModStatus::State::Failed && !mod.info.id.isEmpty()) {
            m_modLoadFailures.insert(mod.info.id, mod.reason);
        }
    }
    refreshMods();
    const std::shared_ptr<const QcContentRegistry> shared = std::move(content->content);
    m_characterContent = shared;
    emit appearancesChanged();

    QcServerConfig config;
    config.worldName = worldName;
    config.seed = seed;
    config.worldDirectory = directory;
    // One extra ring: sections are meshed only once their neighbours are known.
    config.viewDistance = m_viewDistance + 1;
    config.spawnColumn = m_spawnColumn;
    QStringList generatorWarnings;
    auto generator = std::make_unique<QcTerrainGenerator>(shared, config.seed, &generatorWarnings);
    for (const QString &warning : std::as_const(generatorWarnings)) {
        qCWarning(lcGame) << "World generation:" << warning;
    }
    m_server = std::make_unique<QcLocalServerHost>(config, shared, std::move(generator));
    if (!m_server->startError().isEmpty()) {
        const QString error = m_server->startError();
        m_server.reset();
        setPhase(u"error"_s, tr("Cannot load the world: %1").arg(error));
        return;
    }

    m_session = new QcClientSession(m_server->connectClient(), u"Player"_s, this);
    m_session->setAppearance(m_appearance);
    m_session->setCameraMode(m_cameraMode);
    m_meshes = new QcMeshScheduler(this);
    QcPlayerInput intent;
    intent.yaw = m_initialYaw;
    intent.pitch = m_initialPitch;
    m_session->setIntent(intent);

    connect(m_session, &QcClientSession::contentReady, this, &QcGameController::onContentReady);
    connect(m_session, &QcClientSession::chunkLoaded, this, [this](QcChunkPos pos) {
        m_meshes->chunkLoaded(*m_session->world(), pos);
    });
    connect(m_session, &QcClientSession::chunkUnloaded, m_meshes, &QcMeshScheduler::chunkUnloaded);
    connect(m_session, &QcClientSession::blockChanged, this, [this](QcBlockPos pos, QcBlockId previous) {
        m_meshes->blockChanged(*m_session->world(), pos, previous);
    });
    connect(m_session, &QcClientSession::ticked, this, [this] {
        m_sinceTick.restart();
        m_audio->setAmbience(m_session->ambience());
    });
    connect(m_session, &QcClientSession::soundCue, this, [this](const QcSoundCue &cue) {
        const QcVec3 ears = m_session->playerMotion().position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
        // Left and right as seen on screen: the front view looks back at the player.
        const bool front = m_session->cameraMode() == QcCameraMode::Front;
        m_audio->play(cue, ears, m_session->intent().yaw + (front ? 180.0f : 0.0f));
    });
    connect(m_session, &QcClientSession::inventoryChanged, this, &QcGameController::updateInventory);
    connect(m_session, &QcClientSession::craftingOpened, this, [this] { setOverlay(u"inventory"_s); });
    connect(m_session, &QcClientSession::foodChanged, this, &QcGameController::foodChanged);
    connect(m_session, &QcClientSession::machineOpened, this, [this] {
        const auto content = m_session->content();
        const std::optional<QcBlockPos> pos = m_session->machinePos();
        const std::optional<QcBlockId> block = pos && m_session->world() ? m_session->world()->blockAt(*pos) : std::nullopt;
        m_machineLabel = block ? content->translate(content->block(*block).translationKey, m_language) : QString();
        setOverlay(m_session->containerOpen() ? u"chest"_s : u"furnace"_s);
        updateInventory();
        emit machineChanged();
        if (m_fillMachineOnOpen && m_session->containerOpen()) {
            m_fillMachineOnOpen = false;
            for (int slot = 0; slot < 9; ++slot) {
                m_session->moveInventoryItem(slot, QcMsg::MoveInventoryItem::machineInputSlot + slot);
            }
            QTimer::singleShot(1000, this, [this] {
                if (m_screenshotHook) {
                    m_screenshotHook->release();
                }
            });
        } else if (m_fillMachineOnOpen) {
            // Hotbar slot 1 goes into the input, slot 2 onto the fuel.
            m_fillMachineOnOpen = false;
            m_session->moveInventoryItem(0, QcMsg::MoveInventoryItem::machineInputSlot);
            m_session->moveInventoryItem(1, QcMsg::MoveInventoryItem::machineFuelSlot);
            // A few seconds of burning show the flame and the arrow in the screenshot.
            QTimer::singleShot(2500, this, [this] {
                if (m_screenshotHook) {
                    m_screenshotHook->release();
                }
            });
        }
    });
    connect(m_session, &QcClientSession::machineChanged, this, [this] {
        updateInventory();
        emit machineChanged();
    });
    connect(m_session, &QcClientSession::machineClosed, this, [this] {
        if (m_overlay == u"furnace"_s || m_overlay == u"chest"_s) {
            setOverlay({});
        }
        updateInventory();
        emit machineChanged();
    });
    connect(m_session, &QcClientSession::healthChanged, this, [this] {
        if (m_session->isDead()) {
            setOverlay({});
            releaseMouse();
            // Releases on the death screen go to QML; forget held keys so nothing sticks after respawn.
            m_keysDown.clear();
            m_session->setMining(false);
        }
        emit healthChanged();
    });
    connect(m_session, &QcClientSession::chatReceived, this, [this](const QString &text) {
        m_chatLines.append(text);
        // Only the last few lines stay on screen.
        constexpr qsizetype maxChatLines = 8;
        while (m_chatLines.size() > maxChatLines) {
            m_chatLines.removeFirst();
        }
        emit chatLinesChanged();
    });
    connect(m_session, &QcClientSession::stateChanged, this, [this](QcClientSession::State state) {
        if (state == QcClientSession::State::Playing) {
            setPhase(u"playing"_s);
            for (const QString &line : std::as_const(m_startupChat)) {
                // "@<ms> <text>" waits that long after loading (scenes in motion for screenshots);
                // the screenshot is taken only after the last delayed line.
                const qsizetype space = line.indexOf(u' ');
                bool delayed = false;
                const int delayMs = line.startsWith(u'@') && space > 1 ? QStringView(line).mid(1, space - 1).toInt(&delayed) : 0;
                if (!delayed || delayMs < 0) {
                    m_session->sendChat(line);
                    continue;
                }
                if (m_screenshotHook) {
                    m_screenshotHook->hold();
                }
                QTimer::singleShot(delayMs, this, [this, text = line.mid(space + 1)] {
                    if (m_session) {
                        m_session->sendChat(text);
                    }
                    if (m_screenshotHook) {
                        m_screenshotHook->release();
                    }
                });
            }
            if (m_startupOverlay == u"furnace"_s || m_startupOverlay == u"chest"_s) {
                // Uses the block in view once the startup commands have placed it.
                m_fillMachineOnOpen = true;
                if (m_screenshotHook) {
                    m_screenshotHook->hold();
                }
                QTimer::singleShot(1000, this, [this] {
                    if (m_session) {
                        m_session->useItem();
                    }
                });
                // Without a machine in view the screen never opens; take the screenshot anyway.
                QTimer::singleShot(6000, this, [this] {
                    if (!m_fillMachineOnOpen) {
                        return;
                    }
                    m_fillMachineOnOpen = false;
                    qCWarning(lcGame) << "--overlay" << m_startupOverlay << ": no block in view opened";
                    if (m_screenshotHook) {
                        m_screenshotHook->release();
                    }
                });
            } else if (!m_startupOverlay.isEmpty()) {
                setOverlay(m_startupOverlay);
            }
            if (m_benchmarkSeconds > 0) {
                QTimer::singleShot(3000, this, &QcGameController::startBenchmark);
            }
        } else if (state == QcClientSession::State::Disconnected && m_phase != u"menu"_s) {
            releaseMouse();
            setPhase(u"error"_s, tr("Disconnected: %1").arg(m_session->disconnectReason()));
        }
    });
    connect(m_meshes, &QcMeshScheduler::meshReady, this, [this](quint64 key, std::shared_ptr<const QcChunkMesh> mesh) {
        if (m_view) {
            m_view->submitMesh(key, std::move(mesh));
        }
    });
    connect(m_meshes, &QcMeshScheduler::meshRemoved, this, [this](quint64 key) {
        if (m_view) {
            m_view->removeMesh(key);
        }
    });

    if (m_view) {
        m_view->clearMeshes();
        m_view->setFogRange(float(m_viewDistance * QcWorld::chunkSize) * 0.6f, float(m_viewDistance * QcWorld::chunkSize));
    }
    m_sinceTick.start();
    m_session->start();
    setPhase(u"loading"_s, tr("Generating world..."));
    emit sessionStarted(m_session);
}

void QcGameController::onContentReady()
{
    const QcTextureAtlas atlas = QcTextureAtlas::build(*m_session->content());
    const QStringList problems = atlas.problems();
    for (const QString &problem : problems) {
        qCWarning(lcGame) << "Texture:" << problem;
    }
    m_renderTable = std::make_shared<const QcBlockRenderTable>(*m_session->content(), atlas);
    m_atlas = std::make_shared<const QcTextureAtlas>(atlas);
    updateIcons(atlas);
    m_inventory->setContent(m_session->content(), m_language);
    updateInventory();
    m_meshes->setRenderTable(m_renderTable);
    if (m_view) {
        m_view->setAtlas(atlas.image());
    }
    m_audio->setContent(m_session->content());
    m_audio->setMusicEnabled(true);
}

float QcGameController::blockLightAt(QcBlockPos pos) const
{
    return float(m_meshes->blockLight(pos)) / float(QcLightVolume::maxLevel);
}

QcBlockId QcGameController::itemState(const QcContentRegistry &content, QcBlockId block)
{
    // Fences, walls and panes show a piece of a run rather than a lone post.
    if (!content.block(block).connectsTo.isEmpty()) {
        block = content.withProperty(content.withProperty(block, u"east"_s, u"true"_s), u"west"_s, u"true"_s);
    }
    return block;
}

QcEntityMesher::ItemLook QcGameController::itemLook(const QcContentRegistry &content, QcItemId item)
{
    QcEntityMesher::ItemLook look;
    if (!content.isValidItem(item)) {
        return look;
    }
    const QcItemDefinition &definition = content.item(item);
    look.sprite = definition.texture;
    const std::optional<QcBlockId> block = content.blockId(definition.placesBlock);
    if (block) {
        look.glow = float(content.block(*block).lightLevel) / float(QcLightVolume::maxLevel);
    }
    if (block && content.block(*block).itemTexture.isEmpty() && !content.state(*block).boxes.isEmpty()
        && content.block(*block).renderLayer != QcRenderLayer::Invisible) {
        look.cube = true;
        look.faces = content.block(*block).faceTextures;
        if (!content.state(itemState(content, *block)).fullCube) {
            look.boxes = content.state(itemState(content, *block)).boxes;
        }
    }
    return look;
}

std::optional<QcEntityMesher::ItemLook> QcGameController::heldLook(QcItemId item) const
{
    const auto content = m_session->content();
    if (item == qcNoItem || !content || !content->isValidItem(item)) {
        return std::nullopt;
    }
    return itemLook(*content, item);
}

QcEntityMesher::ModelLook QcGameController::playerLook(const QcAppearance &appearance) const
{
    const QcContentRegistry &content = *m_session->content();
    QcEntityMesher::ModelLook look;
    look.model = content.model(u"qtcraft:humanoid"_s);
    look.outfit = content.appearance(appearance.outfit);
    if (!look.outfit || look.outfit->kind != QcAppearanceDefinition::Kind::Outfit) {
        look.outfit = content.appearance(QcAppearance().outfit);
    }
    look.hair = content.appearance(appearance.hair);
    if (look.hair && look.hair->kind != QcAppearanceDefinition::Kind::Hair) {
        look.hair = nullptr;
    }
    look.colors = appearance;
    return look;
}

QcEntityPose QcGameController::localPose(double alpha) const
{
    QcEntityPose pose = m_session->localPose(alpha);
    if (m_forcedPose.isEmpty()) {
        return pose;
    }
    // Screenshot hook: a fixed moment of an animation, independent of the real movement.
    pose.onGround = true;
    if (m_forcedPose == u"walk"_s) {
        pose.speed = QcPhysics::walkSpeed;
        pose.walkDistance = 0.3;
    } else if (m_forcedPose == u"swing"_s) {
        pose.swingProgress = 0.45;
    } else if (m_forcedPose == u"swim"_s) {
        pose.onGround = false;
        pose.inWater = true;
        pose.speed = QcPhysics::swimSpeed;
    } else if (m_forcedPose == u"jump"_s) {
        pose.onGround = false;
    } else if (m_forcedPose == u"sneak"_s) {
        pose.sneaking = true;
    } else if (m_forcedPose == u"death"_s) {
        pose.deathProgress = 0.7;
    }
    return pose;
}

void QcGameController::updateEntities(double alpha, QcCameraMode mode)
{
    if (!m_atlas) {
        return;
    }
    auto mesh = std::make_shared<QcEntityMesh>();
    const QcClientWorld &world = *m_session->world();
    const QcContentRegistry &content = *m_session->content();
    const auto &mobs = content.mobs();
    const QcEntityMesher::UvLookup uv = [this](const QString &texture) { return m_atlas->uv(texture); };
    const double seconds = m_session->worldTime() + alpha * QcPhysics::tickDuration;
    // Entities under a roof are dimmed; a cheap stand-in for sampling the light volume.
    const auto lightAt = [this, &world](const QcVec3 &position) {
        const QcBlockPos feet{int(std::floor(position.x)), int(std::floor(position.y)), int(std::floor(position.z))};
        float sky = 1.0f;
        if (const std::shared_ptr<const QcChunk> chunk = world.chunk(feet.chunk())) {
            sky = chunk->highestNonAir(feet.localX(), feet.localZ()) >= feet.y + 2 ? 0.35f : 1.0f;
        }
        return QcEntityLight(sky, blockLightAt(feet.offset(0, 1, 0)));
    };
    const auto lerpYaw = [](float from, float to, double t) {
        const float delta = std::fmod(to - from + 540.0f, 360.0f) - 180.0f;
        return double(from + delta * float(t));
    };
    const auto appendCreature = [&](const QcClientEntity &entity) {
        const QcVec3 position = lerp(entity.previousPosition, entity.position, alpha);
        QcEntityMesher::Placement placement{position, lerpYaw(entity.previousYaw, entity.yaw, alpha), lightAt(position),
                                            entity.hurtTicks > 0};
        if (entity.mobType >= 0 && entity.mobType < mobs.size() && mobs[entity.mobType].glow > 0) {
            // Glowing creatures pulse slowly, each in its own rhythm; at full glow they are drawn brighter
            // than any lit surface (the shader multiplies, so values above 1 shine).
            constexpr double emission = 1.8;
            const double pulse = 0.75 + 0.25 * std::sin(seconds * 2.2 + double(entity.id % 97));
            const float glow = float(emission * pulse * mobs[entity.mobType].glow / double(QcMobDefinition::maxGlow));
            placement.light = QcEntityLight(placement.light.sky, std::max(placement.light.block, glow));
        }
        QcEntityPose pose = QcClientSession::poseOf(entity, alpha);
        const bool mob = entity.mobType >= 0 && entity.mobType < mobs.size();
        QcEntityMesher::ModelLook look;
        if (mob && !mobs[entity.mobType].skeleton.isEmpty()) {
            const QcMobDefinition &definition = mobs[entity.mobType];
            look.model = content.model(definition.skeleton);
            look.outfit = content.appearance(definition.outfit);
            look.scale = definition.height / QcEntityMesher::modelHeight;
            pose.armsForward = definition.armsForward;
        } else if (!mob) {
            look = playerLook(entity.appearance);
        }
        if (!look.isValid()) {
            // A skeleton or outfit that did not load: the mob's own boxes, else the plain player figure.
            const bool ownParts = mob && !mobs[entity.mobType].parts.isEmpty();
            QcEntityMesher::appendParts(*mesh, ownParts ? mobs[entity.mobType].parts : QcEntityMesher::playerParts(), placement, uv);
            return;
        }
        const std::optional<QcEntityMesher::ItemLook> held = heldLook(entity.held);
        QcEntityMesher::appendModel(*mesh, look, QcAnimator::animateBody(pose, seconds), placement, held ? &*held : nullptr, uv);
    };
    for (const QcClientEntity &entity : m_session->entities()) {
        if (entity.isItem()) {
            QcEntityMesher::appendItem(*mesh, entity, itemLook(content, entity.item.item), alpha, lightAt(entity.position), seconds, uv);
        } else if (entity.isFallingBlock()) {
            QcEntityMesher::ItemLook look;
            look.cube = true;
            look.faces = content.block(entity.block).faceTextures;
            const QcVec3 position = lerp(entity.previousPosition, entity.position, alpha);
            QcEntityMesher::appendBlock(*mesh, look, position, lightAt(position), uv);
        } else {
            appendCreature(entity);
        }
    }
    for (const QcClientEntity &entity : m_session->dyingEntities()) {
        appendCreature(entity);
    }
    // In third person the player sees their own character.
    if (mode != QcCameraMode::FirstPerson && !m_session->isDead()) {
        const QcVec3 feet = lerp(m_session->previousPlayerMotion().position, m_session->playerMotion().position, alpha);
        const QcItemStack &selected = m_session->inventory().selectedStack();
        const std::optional<QcEntityMesher::ItemLook> held = heldLook(selected.isEmpty() ? qcNoItem : selected.item);
        QcEntityMesher::appendModel(*mesh, playerLook(m_appearance), QcAnimator::animateBody(localPose(alpha), seconds),
                                    {feet, double(m_session->intent().yaw), lightAt(feet), false}, held ? &*held : nullptr, uv);
    }
    m_view->setEntityMesh(std::move(mesh));
}

void QcGameController::updateHand(double alpha, const QcVec3 &eye)
{
    if (!m_atlas || m_session->isDead()) {
        m_view->setHandMesh(nullptr);
        return;
    }
    auto mesh = std::make_shared<QcEntityMesh>();
    const QcEntityMesher::UvLookup uv = [this](const QString &texture) { return m_atlas->uv(texture); };
    const QcItemStack &selected = m_session->inventory().selectedStack();
    const std::optional<QcEntityMesher::ItemLook> held = heldLook(selected.isEmpty() ? qcNoItem : selected.item);
    const QcClientSession::HandTimers timers = m_session->handTimers(alpha);
    const double seconds = m_session->worldTime() + alpha * QcPhysics::tickDuration;
    const QcAnimator::HandPose hand = QcAnimator::animateHand(localPose(alpha), seconds, timers.switchProgress, timers.landing, timers.use);
    const QcBlockPos feet{int(std::floor(eye.x)), int(std::floor(eye.y)), int(std::floor(eye.z))};
    QcEntityLight light(1.0f, blockLightAt(feet));
    if (const std::shared_ptr<const QcChunk> chunk = m_session->world()->chunk(feet.chunk())) {
        light.sky = chunk->highestNonAir(feet.localX(), feet.localZ()) > feet.y ? 0.45f : 1.0f;
    }
    QcEntityMesher::appendHand(*mesh, playerLook(m_appearance), held ? &*held : nullptr, hand, eye, m_session->intent().yaw,
                               m_session->intent().pitch, light, uv);
    m_view->setHandMesh(std::move(mesh));
}

const QcContentRegistry &QcGameController::characterContent() const
{
    if (m_session && m_session->content()) {
        return *m_session->content();
    }
    if (!m_characterContent) {
        QStringList warnings;
        QcResult<QcModLoader::Result> loaded = QcModLoader::load(m_modsDirectory, warnings);
        for (const QString &warning : std::as_const(warnings)) {
            qCWarning(lcGame) << "Content:" << warning;
        }
        m_characterContent = loaded ? std::shared_ptr<const QcContentRegistry>(loaded->content)
                                    : std::make_shared<const QcContentRegistry>();
    }
    return *m_characterContent;
}

void QcGameController::resetCharacterContent()
{
    m_characterContent.reset();
    emit appearancesChanged();
}

QVariantList QcGameController::appearanceList(QcAppearanceDefinition::Kind kind) const
{
    QVariantList list;
    const QcContentRegistry &content = characterContent();
    for (const QcAppearanceDefinition &definition : content.appearances()) {
        if (definition.kind == kind && definition.playable) {
            list.append(QVariantMap{{u"name"_s, definition.name}, {u"label"_s, content.translate(definition.translationKey, m_language)}});
        }
    }
    return list;
}

QVariantList QcGameController::outfits() const
{
    return appearanceList(QcAppearanceDefinition::Kind::Outfit);
}

QVariantList QcGameController::hairStyles() const
{
    QVariantList list = appearanceList(QcAppearanceDefinition::Kind::Hair);
    list.prepend(QVariantMap{{u"name"_s, QString()}, {u"label"_s, tr("No hair")}});
    return list;
}

QString QcGameController::cameraMode() const
{
    return QString::fromLatin1(QcViewCamera::name(m_cameraMode));
}

void QcGameController::setCameraMode(const QString &mode)
{
    const std::optional<QcCameraMode> parsed = QcViewCamera::fromName(mode);
    if (!parsed || *parsed == m_cameraMode) {
        return;
    }
    m_cameraMode = *parsed;
    if (m_session) {
        m_session->setCameraMode(m_cameraMode);
    }
    emit settingsChanged();
}

QVariantMap QcGameController::character() const
{
    const auto hex = [](quint32 rgb) { return u"#%1"_s.arg(uint(rgb & 0xFFFFFFu), 6, 16, QChar(u'0')); };
    return {{u"outfit"_s, m_appearance.outfit},          {u"hair"_s, m_appearance.hair},
            {u"skin"_s, hex(m_appearance.skinColor)},    {u"hairColor"_s, hex(m_appearance.hairColor)},
            {u"shirt"_s, hex(m_appearance.shirtColor)},  {u"pants"_s, hex(m_appearance.pantsColor)}};
}

void QcGameController::setCharacter(const QVariantMap &character)
{
    QcAppearance appearance = m_appearance;
    const auto color = [&character](const QString &key, quint32 fallback) {
        const QString text = character.value(key).toString();
        bool ok = false;
        const quint32 rgb = text.startsWith(u'#') && text.size() == 7 ? QStringView(text).mid(1).toUInt(&ok, 16) : 0;
        return ok ? rgb : fallback;
    };
    appearance.outfit = character.value(u"outfit"_s, appearance.outfit).toString().left(QcAppearance::maxNameLength);
    appearance.hair = character.value(u"hair"_s, appearance.hair).toString().left(QcAppearance::maxNameLength);
    appearance.skinColor = color(u"skin"_s, appearance.skinColor);
    appearance.hairColor = color(u"hairColor"_s, appearance.hairColor);
    appearance.shirtColor = color(u"shirt"_s, appearance.shirtColor);
    appearance.pantsColor = color(u"pants"_s, appearance.pantsColor);
    if (appearance == m_appearance) {
        return;
    }
    m_appearance = appearance;
    if (m_session) {
        m_session->setAppearance(m_appearance);
    }
    emit settingsChanged();
}

void QcGameController::updateSky(double alpha)
{
    const double time = m_session->worldTime() + alpha * QcPhysics::tickDuration;
    const double daylight = QcDayCycle::daylight(time);
    // Night sky dark blue, day sky light blue; fog uses the same colour.
    const double t = (daylight - QcDayCycle::minimumDaylight) / (1.0 - QcDayCycle::minimumDaylight);
    const QColor sky = QColor::fromRgbF(float(0.03 + 0.50 * t), float(0.04 + 0.70 * t), float(0.12 + 0.80 * t));
    m_view->setDaylight(float(daylight));
    m_view->setSkyColor(sky);
    const QString label = QcDayCycle::isNight(time) ? tr("Night") : tr("Day");
    if (label != m_timeOfDay) {
        m_timeOfDay = label;
        emit frameStateChanged();
    }
}

void QcGameController::updateIcons(const QcTextureAtlas &atlas)
{
    if (!m_iconProvider) {
        return;
    }
    QHash<QString, QImage> icons;
    const QcContentRegistry &content = *m_session->content();
    const std::array<QImage, 6> shaded = QcBlockIcon::shadedAtlases(atlas.image());
    for (const QcItemDefinition &item : content.items()) {
        // Blocks show their shape, unless they bring a flat icon (torches, doors) or are plants.
        const std::optional<QcBlockId> block = content.blockId(item.placesBlock);
        const bool model = block && m_renderTable && content.block(*block).itemTexture.isEmpty()
                           && content.block(*block).renderLayer != QcRenderLayer::Invisible
                           && !content.state(*block).boxes.isEmpty();
        icons.insert(item.name, model ? QcBlockIcon::render(m_renderTable->entry(itemState(content, *block)), shaded)
                                      : atlas.tile(item.texture));
    }
    m_iconProvider->setIcons(std::move(icons));
}

QcStatus QcGameController::stopSession()
{
    QcStatus saved;
    releaseMouse();
    m_audio->setMusicEnabled(false);
    m_audio->stopWorldSounds();
    if (m_meshes) {
        m_meshes->waitForJobs();
        delete m_meshes;
        m_meshes = nullptr;
    }
    if (m_session) {
        m_session->disconnectFromServer();
        delete m_session;
        m_session = nullptr;
    }
    if (m_server) {
        saved = m_server->shutdown();
        m_server.reset();
    }
    if (m_view) {
        m_view->clearMeshes();
    }
    // Back in the menu: the menu's own content instead of the last world's sounds.
    if (m_audio->isActive() && m_characterContent) {
        m_audio->setContent(m_characterContent);
    }
    m_keysDown.clear();
    return saved;
}

void QcGameController::leaveGame()
{
    if (const QcStatus saved = stopSession(); !saved) {
        setPhase(u"error"_s, tr("The world could not be saved: %1").arg(saved.error()));
        return;
    }
    setPhase(u"menu"_s);
}

void QcGameController::captureMouse()
{
    if (m_mouseCaptured || !m_window || m_phase != u"playing"_s || !m_inputEnabled) {
        return;
    }
    m_mouseCaptured = true;
    m_window->setCursor(Qt::BlankCursor);
    // With platform deltas the cursor is detached and stays put; elsewhere motion is measured
    // against the window centre.
    if (!QcRelativeMouse::hasPlatformDeltas()) {
        QCursor::setPos(m_window->mapToGlobal(QPoint(m_window->width() / 2, m_window->height() / 2)));
    }
    m_relativeMouse.setCaptured(true);
    emit mouseCapturedChanged();
}

void QcGameController::releaseMouse()
{
    if (!m_mouseCaptured) {
        return;
    }
    m_mouseCaptured = false;
    m_relativeMouse.setCaptured(false);
    if (m_window) {
        m_window->unsetCursor();
    }
    emit mouseCapturedChanged();
}

bool QcGameController::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_window && event->type() == QEvent::MouseButtonRelease && !m_mouseCaptured) {
        clickIfButton(static_cast<QMouseEvent *>(event)->scenePosition());
    }
    // Overlays (inventory, pause menu, chat) and the death screen are QML items that take keyboard and
    // mouse themselves.
    if (watched != m_window || !m_session || m_phase != u"playing"_s || !m_inputEnabled || !m_overlay.isEmpty()
        || m_session->isDead()) {
        return QObject::eventFilter(watched, event);
    }
    switch (event->type()) {
    case QEvent::KeyPress: {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->isAutoRepeat()) {
            return false;
        }
        switch (key->key()) {
        case Qt::Key_Escape:
            setOverlay(u"pause"_s);
            return true;
        case Qt::Key_E:
            setOverlay(u"inventory"_s);
            return true;
        case Qt::Key_T:
        case Qt::Key_Return:
            setOverlay(u"chat"_s);
            return true;
        case Qt::Key_R:
            m_session->useItem();
            return true;
        case Qt::Key_F:
            m_session->setMining(true);
            return true;
        case Qt::Key_F5: {
            // Cycles first person, behind, front; the choice is kept in the settings.
            const QcCameraMode next = QcViewCamera::next(m_session->cameraMode());
            setCameraMode(QString::fromLatin1(QcViewCamera::name(next)));
            m_session->setCameraMode(next);
            saveSettings();
            return true;
        }
        case Qt::Key_Q:
            // Q throws one item, Ctrl+Q the whole stack.
            m_session->dropItem(key->modifiers().testFlag(Qt::ControlModifier));
            return true;
        default:
            break;
        }
        if (key->key() >= Qt::Key_1 && key->key() <= Qt::Key_9) {
            m_session->selectHotbarSlot(key->key() - Qt::Key_1);
            return true;
        }
        m_keysDown.insert(key->key());
        return false;
    }
    case QEvent::KeyRelease: {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->isAutoRepeat()) {
            return false;
        }
        if (key->key() == Qt::Key_F) {
            m_session->setMining(false);
        }
        m_keysDown.remove(key->key());
        return false;
    }
    case QEvent::MouseButtonPress:
        if (!m_mouseCaptured) {
            captureMouse();
            return true;
        }
        handleMouseButton(static_cast<QMouseEvent *>(event)->button(), true);
        return true;
    case QEvent::MouseButtonRelease:
        if (m_mouseCaptured) {
            handleMouseButton(static_cast<QMouseEvent *>(event)->button(), false);
            return true;
        }
        return false;
    case QEvent::Wheel: {
        if (!m_mouseCaptured) {
            return false;
        }
        const int steps = static_cast<QWheelEvent *>(event)->angleDelta().y() / 120;
        if (steps != 0) {
            const int slot = (m_session->inventory().selectedHotbarSlot() - steps) % QcInventory::hotbarSize;
            m_session->selectHotbarSlot(slot < 0 ? slot + QcInventory::hotbarSize : slot);
        }
        return true;
    }
    case QEvent::MouseMove: {
        if (!m_mouseCaptured) {
            return false;
        }
        // With platform deltas the view turns once per frame in onFrame().
        if (QcRelativeMouse::hasPlatformDeltas()) {
            return true;
        }
        auto *mouse = static_cast<QMouseEvent *>(event);
        const QPointF centre(m_window->width() / 2.0, m_window->height() / 2.0);
        const QPointF delta = mouse->position() - centre;
        if (delta.manhattanLength() > 0.0) {
            m_session->setIntent(QcMouseLook::apply(m_session->intent(), delta, m_mouseSensitivity, m_invertMouse));
            QCursor::setPos(m_window->mapToGlobal(centre.toPoint()));
        }
        return true;
    }
    default:
        return QObject::eventFilter(watched, event);
    }
}

void QcGameController::handleMouseButton(Qt::MouseButton button, bool pressed)
{
    if (button == Qt::LeftButton) {
        // A click on a creature hits it; holding on a block mines.
        if (pressed && m_session->attack()) {
            return;
        }
        m_session->setMining(pressed);
    } else if (button == Qt::RightButton && pressed) {
        m_session->useItem();
    }
}

void QcGameController::updateIntentFromKeys(float frameSeconds)
{
    const auto down = [this](int key) { return m_keysDown.contains(key); };
    QcPlayerInput intent = m_session->intent();
    intent.forward = float(int(down(Qt::Key_W)) - int(down(Qt::Key_S)));
    intent.strafe = float(int(down(Qt::Key_D)) - int(down(Qt::Key_A)));
    intent.jump = down(Qt::Key_Space);
    intent.sneak = down(Qt::Key_Shift);
    intent.sprint = down(Qt::Key_Control) || down(Qt::Key_Meta);
    const float look = keyboardLookSpeed * frameSeconds;
    intent.yaw += look * float(int(down(Qt::Key_Right)) - int(down(Qt::Key_Left)));
    intent.pitch = std::clamp(intent.pitch + look * float(int(down(Qt::Key_Up)) - int(down(Qt::Key_Down))), -89.9f, 89.9f);
    m_session->setIntent(intent);
}

void QcGameController::startBenchmark()
{
    if (!m_session) {
        return;
    }
    m_benchmarkFrameNs.clear();
    if (m_server) {
        m_server->resetTickTimes();
    }
    m_meshes->resetStats();
    if (!m_benchmarkWalk) {
        m_benchmarkFrame.start();
        m_benchmarkRunning = true;
        QTimer::singleShot(m_benchmarkSeconds * 1000, this, &QcGameController::finishBenchmark);
        if (m_benchmarkLight) {
            // Four times a second a torch appears or vanishes on a ring around the player, so light
            // spreads and goes away again across section and chunk borders all the time.
            auto *torches = new QTimer(this);
            auto step = std::make_shared<int>(0);
            connect(torches, &QTimer::timeout, this, [this, torches, step] {
                if (!m_benchmarkRunning || !m_session) {
                    torches->deleteLater();
                    return;
                }
                constexpr int ring = 12;
                const int index = (*step / 2) % ring;
                const double angle = index * 2.0 * std::numbers::pi / ring;
                const int x = int(std::lround(std::cos(angle) * 9.0));
                const int z = int(std::lround(std::sin(angle) * 9.0));
                m_session->sendChat(u"/setblock ~%1 ~ ~%2 %3"_s.arg(x).arg(z).arg(*step % 2 == 0 ? u"torch"_s : u"air"_s));
                ++*step;
            });
            torches->start(250);
        }
        if (m_benchmarkLiquids) {
            // Every half second a source appears four metres up on a ring around the player (every
            // third one lava) or an earlier one is taken away, so the floor floods and drains all
            // the time.
            auto *sources = new QTimer(this);
            auto step = std::make_shared<int>(0);
            connect(sources, &QTimer::timeout, this, [this, sources, step] {
                if (!m_benchmarkRunning || !m_session) {
                    sources->deleteLater();
                    return;
                }
                constexpr int ring = 6;
                const int index = (*step / 2) % ring;
                const double angle = index * 2.0 * std::numbers::pi / ring;
                const int x = int(std::lround(std::cos(angle) * 8.0));
                const int z = int(std::lround(std::sin(angle) * 8.0));
                const QString source = index % 3 == 2 ? u"lava"_s : u"water"_s;
                m_session->sendChat(u"/setblock ~%1 ~4 ~%2 %3"_s.arg(x).arg(z).arg(*step % 2 == 0 ? source : u"air"_s));
                ++*step;
            });
            sources->start(500);
        }
        return;
    }
    m_inputEnabled = true;
    m_keysDown = {Qt::Key_W, Qt::Key_Control, Qt::Key_Space};
    m_benchmarkFrameNs.reserve(qsizetype(m_benchmarkSeconds) * 240);
    m_benchmarkFrame.start();
    m_benchmarkRunning = true;
    m_benchmarkCheckpoint = m_session->playerMotion().position;
    QTimer::singleShot(m_benchmarkSeconds * 1000, this, &QcGameController::finishBenchmark);
    // Turn away when stuck at a tree or cliff, so the walk keeps loading new chunks.
    auto *steer = new QTimer(this);
    connect(steer, &QTimer::timeout, this, [this, steer] {
        if (!m_benchmarkRunning || !m_session) {
            steer->deleteLater();
            return;
        }
        const QcVec3 position = m_session->playerMotion().position;
        const QcVec3 moved = position - m_benchmarkCheckpoint;
        if (std::hypot(moved.x, moved.z) < 4.0) {
            QcPlayerInput intent = m_session->intent();
            intent.yaw = std::fmod(intent.yaw + 67.0f, 360.0f);
            m_session->setIntent(intent);
        }
        m_benchmarkCheckpoint = position;
    });
    steer->start(1500);
}

void QcGameController::finishBenchmark()
{
    m_benchmarkRunning = false;
    m_keysDown.clear();
    QList<qint64> sorted = m_benchmarkFrameNs;
    std::ranges::sort(sorted);
    if (sorted.isEmpty()) {
        qCWarning(lcGame) << "Benchmark: no frames rendered";
        QCoreApplication::exit(1);
        return;
    }
    qint64 total = 0;
    for (const qint64 ns : std::as_const(sorted)) {
        total += ns;
    }
    const double averageFps = double(sorted.size()) * 1e9 / double(total);
    const double p99Ms = double(sorted[std::min(sorted.size() - 1, sorted.size() * 99 / 100)]) / 1e6;
    const double worstMs = double(sorted.constLast()) / 1e6;
    const QcVec3 position = m_session ? m_session->playerMotion().position : QcVec3{};
    const double tickMs = m_server ? m_server->tickAverageMs() : 0.0;
    const double tickWorstMs = m_server ? m_server->tickWorstMs() : 0.0;
    const double averageMs = double(total) / double(sorted.size()) / 1e6;
    const qsizetype entities = m_session ? m_session->entities().size() : 0;
    // Block changes until their sections show (with their light), the most sections and light jobs
    // waiting at once, and the light thread's work per job.
    const QcMeshScheduler::Stats mesh = m_meshes->stats();
    const double latencyMs = mesh.latencySamples > 0 ? mesh.latencyTotalMs / double(mesh.latencySamples) : 0.0;
    const double lightMs = mesh.lightJobs > 0 ? mesh.lightTotalMs / double(mesh.lightJobs) : 0.0;
    qCInfo(lcGame).noquote() << u"BENCH frame_avg_ms=%1 frame_p99_ms=%2 tick_avg_ms=%3 tick_worst_ms=%4 peak_rss_mb=%5 entities=%6 "
                                u"change_mesh_avg_ms=%7 change_mesh_max_ms=%8 change_sections=%9 mesh_backlog_max=%10 "
                                u"light_jobs=%11 light_job_avg_ms=%12 light_job_max_ms=%13"_s
                                    .arg(averageMs, 0, 'f', 3)
                                    .arg(p99Ms, 0, 'f', 3)
                                    .arg(tickMs, 0, 'f', 3)
                                    .arg(tickWorstMs, 0, 'f', 3)
                                    .arg(peakMemoryMegabytes(), 0, 'f', 1)
                                    .arg(entities)
                                    .arg(latencyMs, 0, 'f', 3)
                                    .arg(mesh.latencyWorstMs, 0, 'f', 3)
                                    .arg(mesh.latencySamples)
                                    .arg(mesh.worstBacklog)
                                    .arg(mesh.lightJobs)
                                    .arg(lightMs, 0, 'f', 3)
                                    .arg(mesh.lightWorstMs, 0, 'f', 3);
    const QString report = u"Benchmark: %1 frames in %2 s, average %3 FPS, 99th percentile frame %4 ms (%5 FPS), worst %6 ms, "
                           u"view distance %7, walked to %8 / %9, %10 chunks loaded"_s.arg(sorted.size())
                               .arg(m_benchmarkSeconds)
                               .arg(averageFps, 0, 'f', 1)
                               .arg(p99Ms, 0, 'f', 2)
                               .arg(1000.0 / p99Ms, 0, 'f', 1)
                               .arg(worstMs, 0, 'f', 2)
                               .arg(m_viewDistance)
                               .arg(position.x, 0, 'f', 0)
                               .arg(position.z, 0, 'f', 0)
                               .arg(m_session && m_session->world() ? m_session->world()->chunkCount() : 0);
    qCInfo(lcGame).noquote() << report;
    QCoreApplication::quit();
}

void QcGameController::onFrame()
{
    if (m_benchmarkRunning) {
        const qint64 ns = m_benchmarkFrame.nsecsElapsed();
        m_benchmarkFrameNs.append(ns);
        if (ns > 100'000'000) {
            // A hidden or covered window may stall frames for reasons outside the game.
            qCInfo(lcGame) << "Benchmark: frame" << m_benchmarkFrameNs.size() << "took" << ns / 1'000'000 << "ms, window"
                           << (m_window && m_window->isExposed() ? "exposed" : "not exposed") << "visibility"
                           << (m_window ? m_window->visibility() : QWindow::Hidden) << "active" << (m_window && m_window->isActive());
        }
        m_benchmarkFrame.restart();
    }
    const float frameSeconds = float(std::min<qint64>(m_frameClock.restart(), 100)) / 1000.0f;
    ++m_framesSinceFps;
    if (m_fpsClock.elapsed() >= 500) {
        m_fps = m_framesSinceFps * 1000.0 / double(m_fpsClock.restart());
        m_framesSinceFps = 0;
    }
    // Taken every frame, so motion from a frame without a world never turns the view later.
    const QPointF mouseDelta = QcRelativeMouse::hasPlatformDeltas() ? m_relativeMouse.takeDelta() : QPointF();
    if (!m_session || !m_view || !m_session->world()) {
        return;
    }
    if (m_mouseCaptured && !mouseDelta.isNull()) {
        m_session->setIntent(QcMouseLook::apply(m_session->intent(), mouseDelta, m_mouseSensitivity, m_invertMouse));
    }
    if (m_inputEnabled && m_phase == u"playing"_s) {
        updateIntentFromKeys(frameSeconds);
    }

    const double alpha = std::clamp(double(m_sinceTick.elapsed()) / (QcPhysics::tickDuration * 1000.0), 0.0, 1.0);
    const QcVec3 feet = lerp(m_session->previousPlayerMotion().position, m_session->playerMotion().position, alpha);
    const QcVec3 eye = feet + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    // The control interface may switch the view too; the session holds the current mode.
    const QcCameraMode mode = m_session->isDead() ? QcCameraMode::FirstPerson : m_session->cameraMode();
    const QcViewCamera::Placement placed =
        QcViewCamera::place(eye, m_session->intent().yaw, m_session->intent().pitch, mode, *m_session->world());
    QcCamera camera = m_view->camera();
    camera.position = QVector3D(float(placed.position.x), float(placed.position.y), float(placed.position.z));
    camera.yaw = placed.yaw;
    camera.pitch = placed.pitch;
    camera.fieldOfView = float(m_fieldOfView);
    m_view->setCamera(camera);

    updateEntities(alpha, mode);
    if (mode == QcCameraMode::FirstPerson) {
        updateHand(alpha, eye);
    } else {
        m_view->setHandMesh(nullptr);
    }
    updateSky(alpha);
    const std::optional<QcRaycastHit> &target = m_session->target();
    const std::optional<quint32> targetEntity = m_session->targetEntity();
    QList<QcAabb> selection;
    if (target && !targetEntity) {
        const std::optional<QcBlockId> block = m_session->world()->blockAt(target->block);
        const QcVec3 corner{double(target->block.x), double(target->block.y), double(target->block.z)};
        if (block && m_session->content()->isValidBlock(*block)) {
            const QList<QcAabb> boxes = m_session->content()->state(*block).targetBoxes();
            for (const QcAabb &box : boxes) {
                selection.append(box.moved(corner));
            }
        }
    }
    m_view->setSelection(selection);
    QString targetLabel;
    if (targetEntity) {
        const QcClientEntity entity = m_session->entities().value(*targetEntity);
        const auto &mobs = m_session->content()->mobs();
        targetLabel = entity.mobType >= 0 && entity.mobType < mobs.size()
                          ? m_session->content()->translate(mobs[entity.mobType].translationKey, m_language)
                          : entity.name;
        targetLabel += u" (%1)"_s.arg(qRound(entity.health));
    } else if (target) {
        if (const std::optional<QcBlockId> block = m_session->world()->blockAt(target->block)) {
            const QcBlockDefinition &definition = m_session->content()->block(*block);
            targetLabel = m_session->content()->translate(definition.translationKey, m_language);
        }
    }
    if (targetLabel != m_targetLabel || m_session->miningProgress() != m_miningProgress) {
        m_targetLabel = targetLabel;
        m_miningProgress = m_session->miningProgress();
        emit frameStateChanged();
    }

    m_meshes->setFocus({feet.x, feet.y, feet.z});
    m_meshes->pump(*m_session->world());

    m_debugText = u"%1 FPS | XYZ %2 / %3 / %4 | chunks %5 | meshes %6 (pending %7)"_s.arg(qRound(m_fps))
                      .arg(feet.x, 0, 'f', 1)
                      .arg(feet.y, 0, 'f', 1)
                      .arg(feet.z, 0, 'f', 1)
                      .arg(m_session->world()->chunkCount())
                      .arg(m_view->meshCount())
                      .arg(m_meshes->dirtyCount() + m_meshes->jobsInFlight());
    emit debugTextChanged();

    const int ring = 2 * (m_viewDistance + 1) + 1;
    const bool allChunksArrived = m_session->world()->chunkCount() >= qsizetype(ring) * ring;
    if (m_screenshotHook && !m_screenshotReleased && m_phase == u"playing"_s && allChunksArrived
        && m_meshes->dirtyCount() == 0 && m_meshes->jobsInFlight() == 0) {
        m_screenshotReleased = true;
        m_screenshotHook->release();
    }
}

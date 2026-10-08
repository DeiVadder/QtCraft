#include "QcGameController.h"

#include "QcModLoader.h"
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

using namespace Qt::StringLiterals;

namespace {

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
    emit settingsChanged();
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
}

void QcGameController::openModsFolder()
{
    if (!m_modsDirectory.isEmpty() && QDir().mkpath(m_modsDirectory)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_modsDirectory));
    }
}

void QcGameController::createWorld(const QString &worldName, const QString &seedText)
{
    const QcWorldLibrary library(QcWorldLibrary::defaultRoot());
    const QString name = worldName.trimmed().isEmpty() ? tr("New World") : worldName.trimmed();
    const QcResult<QcWorldLibrary::Entry> entry = library.create(name, seedFromText(seedText));
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
    if (m_session) {
        m_session->moveInventoryItem(from, to, count);
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
    m_inventory->setInventory(m_session->inventory(), m_session->craftingGrid(), m_session->craftingResult());
    // Recipes the inventory can make right now, for the recipe list next to the grid.
    m_craftableRecipes.clear();
    const auto content = m_session->content();
    if (content) {
        for (const QcRecipeDefinition &recipe : content->recipes()) {
            if (QcCrafting::gridSizeNeeded(recipe) > m_session->craftingSize()) {
                continue;
            }
            int possible = 64;
            const QList<std::pair<QString, int>> needed = QcCrafting::requirements(recipe);
            for (const auto &[name, count] : std::as_const(needed)) {
                const std::optional<QcItemId> item = content->itemId(name);
                possible = std::min(possible, item ? m_session->inventory().countOf(*item) / count : 0);
            }
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
        // Leaving the inventory hands back the grid contents (and closes a crafting table).
        if (m_overlay != u"inventory"_s) {
            m_session->closeCrafting();
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
    connect(m_session, &QcClientSession::blockChanged, m_meshes, &QcMeshScheduler::blockChanged);
    connect(m_session, &QcClientSession::ticked, this, [this] { m_sinceTick.restart(); });
    connect(m_session, &QcClientSession::inventoryChanged, this, &QcGameController::updateInventory);
    connect(m_session, &QcClientSession::craftingOpened, this, [this] { setOverlay(u"inventory"_s); });
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
                m_session->sendChat(line);
            }
            if (!m_startupOverlay.isEmpty()) {
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
}

void QcGameController::updateEntities(double alpha)
{
    if (!m_atlas) {
        return;
    }
    auto mesh = std::make_shared<QcChunkMesh>();
    const QcClientWorld &world = *m_session->world();
    const auto &mobs = m_session->content()->mobs();
    const QcEntityMesher::UvLookup uv = [this](const QString &texture) { return m_atlas->uv(texture); };
    for (const QcClientEntity &entity : m_session->entities()) {
        const QList<QcMobPart> &parts =
            entity.mobType >= 0 && entity.mobType < mobs.size() ? mobs[entity.mobType].parts : QcEntityMesher::playerModel();
        // Entities under a roof are dimmed; a cheap stand-in for sampling the light volume.
        const QcBlockPos feet{int(std::floor(entity.position.x)), int(std::floor(entity.position.y)), int(std::floor(entity.position.z))};
        float light = 1.0f;
        if (const std::shared_ptr<const QcChunk> chunk = world.chunk(feet.chunk())) {
            light = chunk->highestNonAir(feet.localX(), feet.localZ()) >= feet.y + 2 ? 0.35f : 1.0f;
        }
        QcEntityMesher::appendEntity(*mesh, entity, parts, alpha, light, uv);
    }
    m_view->setEntityMesh(std::move(mesh));
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
    for (const QcItemDefinition &item : m_session->content()->items()) {
        icons.insert(item.name, atlas.tile(item.texture));
    }
    m_iconProvider->setIcons(std::move(icons));
}

QcStatus QcGameController::stopSession()
{
    QcStatus saved;
    releaseMouse();
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
    QCursor::setPos(m_window->mapToGlobal(QPoint(m_window->width() / 2, m_window->height() / 2)));
    emit mouseCapturedChanged();
}

void QcGameController::releaseMouse()
{
    if (!m_mouseCaptured) {
        return;
    }
    m_mouseCaptured = false;
    if (m_window) {
        m_window->unsetCursor();
    }
    emit mouseCapturedChanged();
}

bool QcGameController::eventFilter(QObject *watched, QEvent *event)
{
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
        auto *mouse = static_cast<QMouseEvent *>(event);
        const QPointF centre(m_window->width() / 2.0, m_window->height() / 2.0);
        const QPointF delta = mouse->position() - centre;
        if (delta.manhattanLength() > 0.0) {
            QcPlayerInput intent = m_session->intent();
            const auto sensitivity = float(m_mouseSensitivity);
            const float vertical = m_invertMouse ? -sensitivity : sensitivity;
            intent.yaw = std::fmod(intent.yaw + float(delta.x()) * sensitivity, 360.0f);
            intent.pitch = std::clamp(intent.pitch - float(delta.y()) * vertical, -89.9f, 89.9f);
            m_session->setIntent(intent);
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
    m_inputEnabled = true;
    m_keysDown = {Qt::Key_W, Qt::Key_Control, Qt::Key_Space};
    m_benchmarkFrameNs.clear();
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
            qCInfo(lcGame) << "Benchmark: frame" << m_benchmarkFrameNs.size() << "took" << ns / 1'000'000 << "ms";
        }
        m_benchmarkFrame.restart();
    }
    const float frameSeconds = float(std::min<qint64>(m_frameClock.restart(), 100)) / 1000.0f;
    ++m_framesSinceFps;
    if (m_fpsClock.elapsed() >= 500) {
        m_fps = m_framesSinceFps * 1000.0 / double(m_fpsClock.restart());
        m_framesSinceFps = 0;
    }
    if (!m_session || !m_view || !m_session->world()) {
        return;
    }
    if (m_inputEnabled && m_phase == u"playing"_s) {
        updateIntentFromKeys(frameSeconds);
    }

    const double alpha = std::clamp(double(m_sinceTick.elapsed()) / (QcPhysics::tickDuration * 1000.0), 0.0, 1.0);
    const QcVec3 feet = lerp(m_session->previousPlayerMotion().position, m_session->playerMotion().position, alpha);
    QcCamera camera = m_view->camera();
    camera.position = QVector3D(float(feet.x), float(feet.y + QcPhysics::playerEyeHeight), float(feet.z));
    camera.yaw = m_session->intent().yaw;
    camera.pitch = m_session->intent().pitch;
    camera.fieldOfView = float(m_fieldOfView);
    m_view->setCamera(camera);

    updateEntities(alpha);
    updateSky(alpha);
    const std::optional<QcRaycastHit> &target = m_session->target();
    const std::optional<quint32> targetEntity = m_session->targetEntity();
    m_view->setSelection(target && !targetEntity ? std::optional<QcBlockPos>(target->block) : std::nullopt);
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

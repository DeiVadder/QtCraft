#include "QcGameController.h"

#include "QcBaseContent.h"
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
#include "QcItemIconProvider.h"
#include "QcWorldLibrary.h"

#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QLocale>
#include <QVariantMap>
#include <QLoggingCategory>
#include <QQuickWindow>

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
    chunks = std::clamp(chunks, 2, 16);
    if (m_viewDistance == chunks) {
        return;
    }
    m_viewDistance = chunks;
    emit viewDistanceChanged();
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

void QcGameController::moveItem(int from, int to)
{
    if (m_session) {
        m_session->moveInventoryItem(from, to);
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
    QcResult<std::shared_ptr<QcContentRegistry>> content = QcBaseContent::load(warnings);
    for (const QString &warning : std::as_const(warnings)) {
        qCWarning(lcGame) << "Content:" << warning;
    }
    if (!content) {
        setPhase(u"error"_s, tr("Cannot load game content: %1").arg(content.error()));
        return;
    }
    const std::shared_ptr<const QcContentRegistry> shared = std::move(*content);

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
    connect(m_session, &QcClientSession::inventoryChanged, this, [this] { m_inventory->setInventory(m_session->inventory()); });
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
    updateIcons(atlas);
    m_inventory->setContent(m_session->content(), m_language);
    m_inventory->setInventory(m_session->inventory());
    m_meshes->setRenderTable(m_renderTable);
    if (m_view) {
        m_view->setAtlas(atlas.image());
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
    // Overlays (inventory, pause menu, chat) are QML items that take keyboard and mouse themselves.
    if (watched != m_window || !m_session || m_phase != u"playing"_s || !m_inputEnabled || !m_overlay.isEmpty()) {
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
            intent.yaw = std::fmod(intent.yaw + float(delta.x()) * mouseSensitivity, 360.0f);
            intent.pitch = std::clamp(intent.pitch - float(delta.y()) * mouseSensitivity, -89.9f, 89.9f);
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

void QcGameController::onFrame()
{
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
    m_view->setCamera(camera);

    const std::optional<QcRaycastHit> &target = m_session->target();
    m_view->setSelection(target ? std::optional<QcBlockPos>(target->block) : std::nullopt);
    QString targetLabel;
    if (target) {
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

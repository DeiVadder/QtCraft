#include "app/preview/QcCharacterPreview.h"

#include "app/preview/QcCharacterPreviewRenderer.h"
#include "clientcore/preview/QcPreviewScene.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/registry/QcContentRegistry.h"

#include <QCursor>
#include <QLoggingCategory>
#include <QQuickWindow>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcPreview, "qtcraft.preview")

} // namespace

QcCharacterPreview::QcCharacterPreview(QQuickItem *parent)
    : QQuickRhiItem(parent)
{
    // The background stays transparent: the screen behind shows around the figure.
    setAlphaBlending(true);
    setSampleCount(4);
    setAcceptedMouseButtons(Qt::LeftButton);
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(m_pacer.intervalMs());
    connect(&m_timer, &QTimer::timeout, this, &QcCharacterPreview::onTimer);
    connect(this, &QQuickItem::windowChanged, this, &QcCharacterPreview::onWindowChanged);
    connect(this, &QQuickItem::widthChanged, this, &QcCharacterPreview::updateTimer);
    connect(this, &QQuickItem::heightChanged, this, &QcCharacterPreview::updateTimer);
    m_clock.start();
}

QQuickRhiItemRenderer *QcCharacterPreview::createRenderer()
{
    return new QcCharacterPreviewRenderer;
}

void QcCharacterPreview::onWindowChanged(QQuickWindow *window)
{
    disconnect(m_windowVisibility);
    disconnect(m_windowActive);
    if (window) {
        m_windowVisibility = connect(window, &QWindow::visibilityChanged, this, &QcCharacterPreview::updateTimer);
        m_windowActive = connect(window, &QWindow::activeChanged, this, &QcCharacterPreview::updateTimer);
    }
    updateTimer();
}

void QcCharacterPreview::setSource(QcPreviewSource *source)
{
    if (m_source == source) {
        return;
    }

    m_source = source;
    m_hookVersion = -1;
    emit sourceChanged();
    updateTimer();
}

void QcCharacterPreview::applyHook()
{
    if (m_hookVersion == m_source->hookVersion()) {
        return;
    }

    m_hookVersion = m_source->hookVersion();
    const QcPreviewHook &hook = m_source->hook();
    if (hook.animation) {
        m_animation = *hook.animation;
    }
    if (hook.side) {
        m_camera.show(*hook.side);
    }
    if (hook.turn) {
        m_camera.setModelYaw(QcPreviewCamera::frontYaw + *hook.turn);
    }
    if (hook.zoom) {
        m_camera.setZoom(*hook.zoom);
    }
    if (hook.time) {
        m_seconds = *hook.time;
    }
    emit animationChanged();
    emit viewChanged();
}

void QcCharacterPreview::setAnimation(const QString &animation)
{
    const std::optional<QcPreviewPose::Animation> parsed = QcPreviewPose::fromName(animation);
    if (!parsed || *parsed == m_animation || (m_source && m_source->hook().animation)) {
        return;
    }

    m_animation = *parsed;
    m_seconds = 0.0;
    emit animationChanged();
    redraw();
}

QcCharacterPreview::Side QcCharacterPreview::side() const
{
    return m_camera.side() == QcPreviewCamera::Side::Front ? Side::Front : Side::Back;
}

void QcCharacterPreview::setZoom(double zoom)
{
    const double before = m_camera.zoom();
    m_camera.setZoom(zoom);
    if (m_camera.zoom() != before) {
        emit viewChanged();
        redraw();
    }
}

void QcCharacterPreview::setInteractive(bool interactive)
{
    if (m_interactive != interactive) {
        m_interactive = interactive;
        setAcceptedMouseButtons(interactive ? Qt::LeftButton : Qt::NoButton);
        emit interactiveChanged();
    }
}

void QcCharacterPreview::setFramesPerSecond(int framesPerSecond)
{
    const int before = m_pacer.framesPerSecond();
    m_pacer.setFramesPerSecond(framesPerSecond);
    if (m_pacer.framesPerSecond() != before) {
        m_timer.setInterval(m_pacer.intervalMs());
        emit framesPerSecondChanged();
    }
}

void QcCharacterPreview::setPaused(bool paused)
{
    if (m_paused != paused) {
        m_paused = paused;
        emit pausedChanged();
        updateTimer();
    }
}

void QcCharacterPreview::showSide(Side side)
{
    m_camera.show(side == Side::Back ? QcPreviewCamera::Side::Back : QcPreviewCamera::Side::Front);
    emit viewChanged();
    redraw();
}

void QcCharacterPreview::resetView()
{
    m_camera.reset();
    emit viewChanged();
    redraw();
}

QcPreviewActivity::State QcCharacterPreview::activityState() const
{
    using Window = QcPreviewActivity::Window;
    Window state = Window::None;
    if (const QQuickWindow *window = this->window()) {
        // The window of an offscreen run is never shown or active; it counts as a shown one.
        if (m_source && m_source->offscreen()) {
            state = Window::Inactive;
        } else if (window->visibility() == QWindow::Hidden) {
            state = Window::Hidden;
        } else if (window->visibility() == QWindow::Minimized) {
            state = Window::Minimised;
        } else {
            state = window->isActive() ? Window::Active : Window::Inactive;
        }
    }
    return {isVisible(), width() > 0.0 && height() > 0.0, m_source != nullptr, state, m_paused, m_source && m_source->forceActive()};
}

bool QcCharacterPreview::isTransparent() const
{
    for (const QQuickItem *item = this; item; item = item->parentItem()) {
        if (item->opacity() <= 0.0) {
            return true;
        }
    }
    return false;
}

void QcCharacterPreview::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickRhiItem::itemChange(change, value);
    if (change == ItemVisibleHasChanged) {
        updateTimer();
    }
}

void QcCharacterPreview::updateTimer()
{
    const QcPreviewActivity::Mode mode = this->mode();
    if (mode == QcPreviewActivity::Mode::Off) {
        m_timer.stop();
        m_pacer.hide();
        m_lastFrameMs = -1;
        m_figureDrawn = false;
        return;
    }

    if (!QcPreviewActivity::timerRuns(mode, m_figureDrawn)) {
        m_timer.stop();
    } else if (!m_timer.isActive()) {
        m_timer.start();
        // The first frame at once, but after the item's properties are all set (QML creation).
        QMetaObject::invokeMethod(this, &QcCharacterPreview::onTimer, Qt::QueuedConnection);
    }
}

void QcCharacterPreview::redraw()
{
    m_figureDrawn = false;
    updateTimer();
}

void QcCharacterPreview::onTimer()
{
    // A screen fading in or out shows nothing of the preview yet; the timer keeps asking.
    if (isTransparent() || !m_pacer.frameDue(mode() != QcPreviewActivity::Mode::Off, m_clock.elapsed())) {
        return;
    }

    QElapsedTimer build;
    build.start();
    const bool hadFigure = hasFigure();
    buildFrame();
    m_source->addBuildNs(build.nsecsElapsed());
    update();

    if (hasFigure() != hadFigure) {
        emit hasFigureChanged();
    }
    if (hasFigure()) {
        m_figureDrawn = true;
        if (!std::exchange(m_loggedFigure, true)) {
            qCInfo(lcPreview).noquote() << u"Character preview %1: first figure with %2 vertices"_s.arg(objectName()).arg(m_mesh->vertices.size());
        }
        // A still picture needs no further frames.
        updateTimer();
    }
}

QcPreviewPose::Gaze QcCharacterPreview::gazeTarget() const
{
    const double aspect = height() > 0.0 ? width() / height() : 1.0;
    if (const std::optional<std::pair<double, double>> &fixed = m_source->hook().gaze) {
        return QcPreviewPose::gazeAt(m_camera, fixed->first, fixed->second, aspect);
    }
    // Only an active window has a pointer to follow; scripted runs never activate theirs.
    if (!window() || !window()->isActive()) {
        return {};
    }

    const QPointF pointer = mapFromGlobal(QCursor::pos());
    const double x = (pointer.x() / width()) * 2.0 - 1.0;
    const double y = 1.0 - (pointer.y() / height()) * 2.0;
    return QcPreviewPose::gazeAt(m_camera, x, y, aspect);
}

void QcCharacterPreview::buildFrame()
{
    // Content, look and held item are read anew every frame, so changes show in the next one.
    applyHook();
    const qint64 now = m_clock.elapsed();
    const double seconds = m_lastFrameMs < 0 ? 0.0 : std::min(0.1, double(now - m_lastFrameMs) / 1000.0);
    m_lastFrameMs = now;
    const bool fixedTime = m_source->hook().time.has_value();
    m_seconds += fixedTime ? 0.0 : seconds;
    // A fixed gaze (test hook) is reached at once; the pointer is followed smoothly.
    const QcPreviewPose::Gaze target = gazeTarget();
    m_gaze = m_source->hook().gaze ? target : QcPreviewPose::approach(m_gaze, target, seconds);

    const std::shared_ptr<const QcContentRegistry> content = m_source->content();
    const std::shared_ptr<const QcTextureAtlas> atlas = m_source->atlas();
    if (!content || !atlas) {
        m_mesh.reset();
        return;
    }
    if (m_atlasVersion != m_source->version()) {
        m_atlasVersion = m_source->version();
        m_atlas = atlas->levels();
    }

    // A fixed moment (test hook) shows the cape at rest on its target, like the pose.
    const QcCapeDrive drive = QcPreviewPose::capeDrive(m_animation);
    if (fixedTime) {
        m_cape.settle(drive, m_seconds);
    } else {
        m_cape.advance(drive, seconds, m_seconds);
    }

    const bool mining = m_animation == QcPreviewPose::Animation::Mine;
    const QcPreviewScene::Figure figure{m_source->appearance(), m_source->heldItem(mining), QcPreviewPose::pose(m_animation, m_seconds, m_gaze),
                                        m_camera.modelYaw(), m_seconds, m_source->equipment(), m_cape.angles()};
    m_mesh = std::make_shared<const QcEntityMesh>(QcPreviewScene::build(*content, figure, [&atlas](const QString &texture) { return atlas->uv(texture); }));
    m_viewCamera = m_camera.camera();
}

void QcCharacterPreview::mousePressEvent(QMouseEvent *event)
{
    if (!m_interactive) {
        event->ignore();
        return;
    }
    m_dragX = event->position().x();
    event->accept();
}

void QcCharacterPreview::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragX) {
        event->ignore();
        return;
    }

    const QcPreviewCamera::Side before = m_camera.side();
    m_camera.drag(event->position().x() - *m_dragX);
    m_dragX = event->position().x();
    if (m_camera.side() != before) {
        emit viewChanged();
    }
    event->accept();
}

void QcCharacterPreview::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragX.reset();
    event->accept();
}

void QcCharacterPreview::wheelEvent(QWheelEvent *event)
{
    if (!m_interactive) {
        event->ignore();
        return;
    }
    setZoom(m_camera.zoom() + QcPreviewCamera::zoomPerStep * double(event->angleDelta().y()) / 120.0);
    event->accept();
}

#include "QcOffscreenRender.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <rhi/qrhi.h>

using namespace Qt::StringLiterals;

namespace {
Q_LOGGING_CATEGORY(lcOffscreen, "qtcraft.offscreen")

// Without a screen rate (offscreen platform): a usual display's 60 Hz.
constexpr qreal fallbackRefreshRate = 60.0;

bool platformWithoutWindows()
{
    return QGuiApplication::platformName() == u"offscreen"_s;
}
} // namespace

QcOffscreenRender::QcOffscreenRender(QQuickWindow &mainWindow, QObject *parent)
    : QObject(parent)
    , m_mainWindow(mainWindow)
    , m_renderControl(std::make_unique<QQuickRenderControl>())
    , m_window(std::make_unique<QQuickWindow>(m_renderControl.get()))
{
    m_window->setScreen(mainWindow.screen());
    m_window->resize(mainWindow.size());
    m_window->contentItem()->setSize(mainWindow.size());
    m_window->setColor(mainWindow.color());

    m_frameTimer.setTimerType(Qt::PreciseTimer);
    // The frame rate of the window on that screen: screenshot runs count frames, and what happens
    // until the picture (flowing liquids, falling sand) should not depend on the mode.
    const qreal refreshRate = mainWindow.screen() && mainWindow.screen()->refreshRate() > 1.0 ? mainWindow.screen()->refreshRate() : fallbackRefreshRate;
    m_frameTimer.setInterval(qRound(1000.0 / refreshRate));
    connect(&m_frameTimer, &QTimer::timeout, this, [this] { renderFrame(false); });
}

QcOffscreenRender::~QcOffscreenRender()
{
    m_frameTimer.stop();
    // The content goes back to the main window, which the QML engine owns and deletes; the
    // scene graph resources go first, while the QRhi still exists.
    m_renderControl->invalidate();
    moveItems(m_window->contentItem(), contentOf(m_mainWindow));

    m_window->setRenderTarget({});
    m_renderTarget.reset();
    m_renderPass.reset();
    m_depthStencil.reset();
    m_texture.reset();
    // The window before its render control, as in Qt's render control examples.
    m_window.reset();
    m_renderControl.reset();
}

bool QcOffscreenRender::requested()
{
    return qEnvironmentVariable("QTCRAFT_OFFSCREEN") == u"1"_s || platformWithoutWindows();
}

void QcOffscreenRender::prepare()
{
#if defined(Q_OS_APPLE)
    // The offscreen platform has no OpenGL and offers Qt Quick only its software renderer, in which
    // the world view (QQuickRhiItem) stays empty; Metal needs no window.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
#endif
    if (platformWithoutWindows()) {
        QQuickWindow::setSceneGraphBackend(u"rhi"_s);
    }
#if defined(Q_OS_MACOS)
    // The native macOS style draws its controls into the AppKit view of the window; the window
    // id of the offscreen platform is no view, and drawing crashes.
    if (platformWithoutWindows() && qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(u"Fusion"_s);
    }
#endif
}

bool QcOffscreenRender::start()
{
    if (!m_renderControl->initialize()) {
        qCCritical(lcOffscreen) << "Cannot render offscreen: no QRhi";
        return false;
    }
    if (!createRenderTarget()) {
        qCCritical(lcOffscreen) << "Cannot render offscreen: no render target";
        return false;
    }

    moveItems(contentOf(m_mainWindow), m_window->contentItem());
    m_frameTimer.start();
    qCInfo(lcOffscreen).noquote() << u"Rendering offscreen with %1, %2x%3 pixels"_s.arg(QString::fromLatin1(m_renderControl->rhi()->backendName()))
                                         .arg(m_texture->pixelSize().width())
                                         .arg(m_texture->pixelSize().height());
    return true;
}

QQuickWindow *QcOffscreenRender::window() const
{
    return m_window.get();
}

QImage QcOffscreenRender::grab()
{
    renderFrame(true);
    return m_lastImage;
}

bool QcOffscreenRender::createRenderTarget()
{
    QRhi *rhi = m_renderControl->rhi();
    const qreal ratio = m_mainWindow.devicePixelRatio();
    const QSize pixelSize = m_mainWindow.size() * ratio;

    m_texture.reset(rhi->newTexture(QRhiTexture::RGBA8, pixelSize, 1, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    m_depthStencil.reset(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, pixelSize, 1));
    if (!m_texture->create() || !m_depthStencil->create()) {
        return false;
    }

    QRhiTextureRenderTargetDescription description{QRhiColorAttachment(m_texture.get())};
    description.setDepthStencilBuffer(m_depthStencil.get());
    m_renderTarget.reset(rhi->newTextureRenderTarget(description));
    m_renderPass.reset(m_renderTarget->newCompatibleRenderPassDescriptor());
    m_renderTarget->setRenderPassDescriptor(m_renderPass.get());
    if (!m_renderTarget->create()) {
        return false;
    }

    QQuickRenderTarget target = QQuickRenderTarget::fromRhiRenderTarget(m_renderTarget.get());
    target.setDevicePixelRatio(ratio);
    m_window->setRenderTarget(target);
    return true;
}

QQuickItem *QcOffscreenRender::contentOf(const QQuickWindow &window)
{
    auto *content = window.property("contentItem").value<QQuickItem *>();
    return content ? content : window.contentItem();
}

void QcOffscreenRender::moveItems(QQuickItem *from, QQuickItem *to)
{
    // QObject parent too: code that looks items up from the window (findChildren) finds them.
    const QList<QQuickItem *> items = from->childItems();
    for (QQuickItem *item : items) {
        item->setParent(to);
        item->setParentItem(to);
    }
}

void QcOffscreenRender::renderFrame(bool readBack)
{
    // polishItems() also emits afterAnimating, which advances the game.
    m_renderControl->polishItems();
    m_renderControl->beginFrame();
    m_renderControl->sync();
    m_renderControl->render();

    QRhiReadbackResult result;
    if (readBack) {
        QRhiResourceUpdateBatch *batch = m_renderControl->rhi()->nextResourceUpdateBatch();
        batch->readBackTexture(QRhiReadbackDescription(m_texture.get()), &result);
        m_renderControl->commandBuffer()->resourceUpdate(batch);
    }
    // Waits until the GPU is done, so the readback is complete afterwards.
    m_renderControl->endFrame();

    if (readBack) {
        const QImage image(reinterpret_cast<const uchar *>(result.data.constData()), result.pixelSize.width(), result.pixelSize.height(),
                           QImage::Format_RGBA8888_Premultiplied);
        m_lastImage = (m_renderControl->rhi()->isYUpInFramebuffer() ? image.mirrored() : image).convertToFormat(QImage::Format_RGB32);
        m_lastImage.setDevicePixelRatio(m_window->devicePixelRatio());
    }
    emit m_window->frameSwapped();
}

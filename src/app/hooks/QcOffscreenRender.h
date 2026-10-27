#pragma once

#include <QImage>
#include <QObject>
#include <QTimer>

#include <memory>

class QQuickItem;
class QQuickRenderControl;
class QQuickWindow;
class QRhiRenderBuffer;
class QRhiRenderPassDescriptor;
class QRhiTexture;
class QRhiTextureRenderTarget;

// Renders the game without a shown window (decision E-229): the content of the main window moves
// into a QQuickWindow driven by a QQuickRenderControl, which draws through QRhi (Metal on macOS)
// into a texture at a fixed frame rate. A window that the system does not show (display off,
// screen locked, window covered) gets no frames; this one does not depend on the display.
// Used for screenshot runs (QTCRAFT_OFFSCREEN=1, and always with QT_QPA_PLATFORM=offscreen, whose
// windows have no QRhi). Input comes only from the script; frame times are not comparable with
// the real window, so benchmarks stay on the window.
class QcOffscreenRender : public QObject
{
    Q_OBJECT

public:
    // The offscreen window takes the main window's size, colour and screen (device pixel ratio).
    explicit QcOffscreenRender(QQuickWindow &mainWindow, QObject *parent = nullptr);
    ~QcOffscreenRender() override;
    Q_DISABLE_COPY_MOVE(QcOffscreenRender)

    // QTCRAFT_OFFSCREEN=1, or a platform without native windows (QT_QPA_PLATFORM=offscreen).
    static bool requested();
    // Call before QML loads when requested(): the graphics API (offscreen QRhi needs no window)
    // and, on platforms without native windows, the controls style.
    static void prepare();

    // Moves the main window's content over and starts rendering; false when no QRhi is available.
    bool start();
    // The window that renders; it emits frameSwapped after every frame.
    QQuickWindow *window() const;
    // Renders a frame and returns it.
    QImage grab();

private:
    bool createRenderTarget();
    // The items of the window's content: the content item of an ApplicationWindow.
    static QQuickItem *contentOf(const QQuickWindow &window);
    static void moveItems(QQuickItem *from, QQuickItem *to);
    void renderFrame(bool readBack);

    QQuickWindow &m_mainWindow;
    std::unique_ptr<QQuickRenderControl> m_renderControl;
    std::unique_ptr<QQuickWindow> m_window;
    std::unique_ptr<QRhiTexture> m_texture;
    std::unique_ptr<QRhiRenderBuffer> m_depthStencil;
    std::unique_ptr<QRhiTextureRenderTarget> m_renderTarget;
    std::unique_ptr<QRhiRenderPassDescriptor> m_renderPass;
    QTimer m_frameTimer;
    QImage m_lastImage;
};

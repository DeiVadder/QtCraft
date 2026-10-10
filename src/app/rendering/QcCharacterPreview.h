#pragma once

#include "QcCamera.h"
#include "QcPreviewActivity.h"
#include "QcEntityMesh.h"
#include "QcPreviewCamera.h"
#include "QcPreviewPacer.h"
#include "QcPreviewPose.h"
#include "QcPreviewSource.h"

#include <QElapsedTimer>
#include <QImage>
#include <QPointer>
#include <QQuickRhiItem>
#include <QTimer>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <optional>

// Live render of the player's character as a UI element (CharacterPreview in QML, decision E-231):
// the same meshes, textures, animations and shaders as other players in the world, drawn with QRhi
// into the item. Dragging turns the figure, the wheel zooms, the head follows the mouse pointer
// while the window is active. It animates only while it is visible in the active window and not
// paused, at most `framesPerSecond` times a second (QcPreviewPacer); in an inactive window or while
// paused the last picture stays (QcPreviewActivity), a hidden preview costs nothing.
class QcCharacterPreview : public QQuickRhiItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(CharacterPreview)
    Q_PROPERTY(QcPreviewSource *source READ source WRITE setSource NOTIFY sourceChanged FINAL)
    // "stand", "walk" or "mine".
    Q_PROPERTY(QString animation READ animation WRITE setAnimation NOTIFY animationChanged FINAL)
    // "front" or "back": the side of the figure that faces the viewer.
    Q_PROPERTY(QString side READ side NOTIFY viewChanged FINAL)
    // 0 shows the whole body, 1 the head.
    Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY viewChanged FINAL)
    // Mouse dragging and the wheel turn and zoom the figure.
    Q_PROPERTY(bool interactive READ interactive WRITE setInteractive NOTIFY interactiveChanged FINAL)
    Q_PROPERTY(int framesPerSecond READ framesPerSecond WRITE setFramesPerSecond NOTIFY framesPerSecondChanged FINAL)
    // Covered by another screen: the picture stays as it is.
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged FINAL)
    // The figure is drawn (false while the content or atlas is still loading).
    Q_PROPERTY(bool hasFigure READ hasFigure NOTIFY hasFigureChanged FINAL)

public:
    explicit QcCharacterPreview(QQuickItem *parent = nullptr);

    QcPreviewSource *source() const { return m_source; }
    void setSource(QcPreviewSource *source);
    QString animation() const { return QcPreviewPose::name(m_animation); }
    void setAnimation(const QString &animation);
    QString side() const;
    double zoom() const { return m_camera.zoom(); }
    void setZoom(double zoom);
    bool interactive() const { return m_interactive; }
    void setInteractive(bool interactive);
    int framesPerSecond() const { return m_pacer.framesPerSecond(); }
    void setFramesPerSecond(int framesPerSecond);
    bool paused() const { return m_paused; }
    void setPaused(bool paused);
    bool hasFigure() const { return m_mesh && !m_mesh->indices.isEmpty(); }

    // Shows the figure straight from "front" or "back".
    Q_INVOKABLE void showSide(const QString &side);
    // The default view: turned a little, no zoom.
    Q_INVOKABLE void resetView();

signals:
    void sourceChanged();
    void animationChanged();
    void viewChanged();
    void interactiveChanged();
    void framesPerSecondChanged();
    void pausedChanged();
    void hasFigureChanged();

protected:
    QQuickRhiItemRenderer *createRenderer() override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    friend class QcCharacterPreviewRenderer;

    QcPreviewActivity::State activityState() const;
    QcPreviewActivity::Mode mode() const { return QcPreviewActivity::mode(activityState()); }
    // Opacity 0 here or in a parent (a screen fading in or out): no frame is drawn.
    bool isTransparent() const;
    // Follows the window's visibility and focus.
    void onWindowChanged(QQuickWindow *window);
    // Starts or stops the frame timer with the activity mode.
    void updateTimer();
    // Draws again in a still mode (the view or animation changed).
    void redraw();
    void onTimer();
    // Advances the animation and the gaze and builds the figure's mesh for the renderer.
    void buildFrame();
    QcPreviewPose::Gaze gazeTarget() const;
    // The source's --preview hook, once per hook version.
    void applyHook();

    QPointer<QcPreviewSource> m_source;
    int m_hookVersion = -1;
    QcPreviewPose::Animation m_animation = QcPreviewPose::Animation::Stand;
    QcPreviewCamera m_camera;
    QcPreviewPacer m_pacer;
    QcPreviewPose::Gaze m_gaze;
    bool m_interactive = true;
    bool m_paused = false;
    // A frame since the preview was last hidden showed the figure.
    bool m_figureDrawn = false;
    bool m_loggedFigure = false;
    QMetaObject::Connection m_windowVisibility;
    QMetaObject::Connection m_windowActive;
    std::optional<double> m_dragX;
    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_lastFrameMs = -1;
    double m_seconds = 0.0;

    // Handed to the renderer in synchronize().
    std::shared_ptr<const QcEntityMesh> m_mesh;
    QcCamera m_viewCamera;
    QImage m_atlas;
    quint64 m_atlasVersion = 0;
};

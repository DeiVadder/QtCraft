#pragma once

#include "QcClientSession.h"
#include "QcPlayerInput.h"
#include "QcWorldView.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QtQmlIntegration/qqmlintegration.h>

#include <array>
#include <memory>
#include <optional>

class QcBlockRenderTable;
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

public:
    static constexpr int defaultViewDistance = 8;
    static constexpr float mouseSensitivity = 0.15f;
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

    Q_INVOKABLE void startSingleplayer(const QString &worldName, const QString &seedText);
    Q_INVOKABLE void leaveGame();
    Q_INVOKABLE void captureMouse();
    Q_INVOKABLE void releaseMouse();

    // Test hooks (command line): initial view direction and the screenshot to take once loaded.
    void setInitialLook(float yaw, float pitch);
    void setSpawnColumn(int x, int z) { m_spawnColumn = std::array<int, 2>{x, z}; }
    void setScreenshotHook(QcScreenshotHook *hook);
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

private:
    void setPhase(const QString &phase, const QString &status = {});
    void attachWindow(QQuickWindow *window);
    void onContentReady();
    void onFrame();
    void updateIntentFromKeys(float frameSeconds);
    void stopSession();

    QPointer<QcWorldView> m_view;
    QPointer<QQuickWindow> m_window;
    QString m_phase = QStringLiteral("menu");
    QString m_statusText;
    QString m_debugText;
    bool m_mouseCaptured = false;
    bool m_inputEnabled = true;
    int m_viewDistance = defaultViewDistance;

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
    QPointer<QcScreenshotHook> m_screenshotHook;
    bool m_screenshotReleased = false;
};

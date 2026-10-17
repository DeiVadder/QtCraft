#pragma once

#include "app/controllers/QcDebugVisuals.h"
#include "app/hooks/QcFrameMonitor.h"
#include "app/models/QcWorldLabelModel.h"
#include "clientcore/debug/QcDebugSnapshot.h"
#include "common/core/QcVec3.h"
#include "common/world/QcWorldLabel.h"

#include <QElapsedTimer>
#include <QFont>
#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

class QcSettingsController;
struct QcSessionParts;

// The debug display in the HUD (GameController.hud.debug, decision E-225): the compact line or the
// full columns of text, the frame and tick graphs and the light values around the target, a few
// times a second; the frame statistics behind them; the server subscription the display needs.
class QcDebugHud : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    // 0 off, 1 compact, 2 full (the settings' level).
    Q_PROPERTY(int level READ level NOTIFY levelChanged FINAL)
    Q_PROPERTY(QString generalText READ generalText NOTIFY textChanged FINAL)
    Q_PROPERTY(QString timingsText READ timingsText NOTIFY textChanged FINAL)
    // Frame intervals and server tick durations in milliseconds, oldest first.
    Q_PROPERTY(QList<qreal> frameGraph READ frameGraph NOTIFY textChanged FINAL)
    Q_PROPERTY(QList<qreal> tickGraph READ tickGraph NOTIFY textChanged FINAL)
    Q_PROPERTY(QFont monoFont READ monoFont CONSTANT FINAL)
    // Light values (sky and block) of the cells around the target, when that visualisation is on.
    Q_PROPERTY(QcWorldLabelModel *lightLabels READ lightLabels CONSTANT FINAL)

public:
    static constexpr qint64 textIntervalMs = 250;
    // Cells around the target's face that show their light.
    static constexpr int lightLabelRadius = 2;

    QcDebugHud(const QcSessionParts &parts, const QcSettingsController &settings, QObject *parent = nullptr);

    int level() const;
    QString generalText() const { return m_generalText; }
    QString timingsText() const { return m_timingsText; }
    QList<qreal> frameGraph() const { return m_frameGraph; }
    QList<qreal> tickGraph() const { return m_tickGraph; }
    QFont monoFont() const;
    QcWorldLabelModel *lightLabels() const { return m_lightLabels; }

    // The statistics log needs the full frame statistics and the server's data.
    void setStatsLogging(bool on);
    // Once per frame after the entities were meshed: records the frame (with the character
    // previews' meshing since the previous one) and returns the render samples (for the benchmark,
    // which needs the full statistics while `benchmarkRunning`).
    QList<QcRenderSample> recordFrame(qint64 entityNs, qint64 previewNs, bool benchmarkRunning);
    double lastGuiCpuMs() const { return m_frames.lastGuiCpuMs(); }
    // Text, graphs and light values, a few times a second while shown; `feet` is the interpolated
    // position of the drawn frame.
    void update(const QcVec3 &feet);
    // The data now, also when nothing is shown (statistics log), for `reader`.
    QcDebugSnapshot snapshot(const QcVec3 &feet, QcFrameMonitor::Reader reader);
    // The server data the display and the log need, after a session started or a setting changed.
    void syncSubscription() const;
    // The debug visualisations in the world, once per frame.
    void updateWorld(double alpha, const QcVec3 &feet);
    // A section got a new mesh (for its flash).
    void meshed(quint64 sectionKey);
    // The session ended: no lines, no flashes.
    void clearWorld() { m_visuals.clear(); }
    // The text is due at once (the level changed).
    void resetClock() { m_textClock.invalidate(); }

signals:
    void levelChanged();
    void textChanged();

private:
    void updateText(const QcVec3 &feet);
    void updateLightLabels();
    // Light labels of the air cells resting on a block around `centre`.
    QList<QcWorldLabel> lightCells(QcBlockPos centre) const;

    const QcSessionParts &m_parts;
    const QcSettingsController &m_settings;
    QcFrameMonitor m_frames;
    QcDebugVisuals m_visuals;
    bool m_statsLogging = false;
    QElapsedTimer m_textClock;
    QString m_generalText;
    QString m_timingsText;
    QList<qreal> m_frameGraph;
    QList<qreal> m_tickGraph;
    QcWorldLabelModel *m_lightLabels = nullptr;
    QList<QcWorldLabel> m_lightCells;
};

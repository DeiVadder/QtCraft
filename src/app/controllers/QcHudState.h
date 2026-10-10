#pragma once

#include "QcDebugHud.h"
#include "QcVec3.h"
#include "QcWorldLabelModel.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

class QcSettingsController;
struct QcSessionParts;

// HUD texts that change while playing (GameController.hud): what the crosshair points at with the
// mining progress, the time of day, the debug display and the last chat lines.
class QcHudState : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(double miningProgress READ miningProgress NOTIFY frameStateChanged FINAL)
    Q_PROPERTY(QString targetLabel READ targetLabel NOTIFY frameStateChanged FINAL)
    Q_PROPERTY(QString timeOfDay READ timeOfDay NOTIFY frameStateChanged FINAL)
    Q_PROPERTY(QStringList chatLines READ chatLines NOTIFY chatLinesChanged FINAL)
    // Labels floating in the world, drawn over it (decision E-222).
    Q_PROPERTY(QcWorldLabelModel *labels READ labels CONSTANT FINAL)
    // The debug display (decision E-225).
    Q_PROPERTY(QcDebugHud *debug READ debug CONSTANT FINAL)

public:
    QcHudState(const QcSessionParts &parts, const QcSettingsController &settings, QObject *parent = nullptr);

    double miningProgress() const { return m_miningProgress; }
    QString targetLabel() const { return m_targetLabel; }
    QString timeOfDay() const { return m_timeOfDay; }
    QStringList chatLines() const { return m_chatLines; }
    QcWorldLabelModel *labels() const { return m_labels; }
    QcDebugHud *debug() const { return m_debug; }
    // Content name under the crosshair ("qtcraft:stone", "mob:qtcraft:pig") for the handbook key.
    QString targetName() const { return m_targetName; }

    // The session's target and mining progress, once per frame.
    void updateTarget();
    void updateTimeOfDay(double alpha);
    void addChatLine(const QString &text);
    // Places the labels near the camera on the view, once per frame (empty while switched off).
    void updateLabels();

signals:
    void frameStateChanged();
    void chatLinesChanged();

private:
    const QcSessionParts &m_parts;
    const QcSettingsController &m_settings;
    double m_miningProgress = 0.0;
    QString m_targetLabel;
    QString m_targetName;
    QString m_timeOfDay;
    QStringList m_chatLines;
    QcWorldLabelModel *m_labels = nullptr;
    QcDebugHud *m_debug = nullptr;
};

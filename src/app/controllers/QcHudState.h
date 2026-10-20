#pragma once

#include "app/debug/QcDebugHud.h"
#include "app/models/QcWorldLabelModel.h"
#include "common/core/QcChunkPos.h"
#include "common/core/QcVec3.h"
#include "common/world/QcWorldLabel.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

class QcSettingsController;
struct QcSessionParts;

// HUD texts that change while playing (GameController.hud): what the crosshair points at with the
// mining progress, the time of day, the dial of a held compass or clock, the debug display and the
// last chat lines.
class QcHudState : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(HudState)
    QML_UNCREATABLE("Provided by GameController.hud")
    Q_PROPERTY(double miningProgress READ miningProgress NOTIFY frameStateChanged FINAL)
    Q_PROPERTY(QString targetLabel READ targetLabel NOTIFY frameStateChanged FINAL)
    Q_PROPERTY(QString timeOfDay READ timeOfDay NOTIFY frameStateChanged FINAL)
    Q_PROPERTY(QStringList chatLines READ chatLines NOTIFY chatLinesChanged FINAL)
    // The dial of the held item (decision E-769), its needle or hand in degrees clockwise from the
    // top (QcDials) and the clock's time ("14:05").
    Q_PROPERTY(Dial dial READ dial NOTIFY dialChanged FINAL)
    Q_PROPERTY(double dialAngle READ dialAngle NOTIFY dialChanged FINAL)
    Q_PROPERTY(QString clockText READ clockText NOTIFY dialChanged FINAL)
    // Labels floating in the world, drawn over it (decision E-222).
    Q_PROPERTY(QcWorldLabelModel *labels READ labels CONSTANT FINAL)
    // The debug display (decision E-225).
    Q_PROPERTY(QcDebugHud *debug READ debug CONSTANT FINAL)

public:
    // HudState.Dial in QML: what the held item shows.
    enum class Dial { None, Compass, Clock };
    Q_ENUM(Dial)

    QcHudState(const QcSessionParts &parts, const QcSettingsController &settings, QObject *parent = nullptr);

    double miningProgress() const { return m_miningProgress; }
    QString targetLabel() const { return m_targetLabel; }
    QString timeOfDay() const { return m_timeOfDay; }
    QStringList chatLines() const { return m_chatLines; }
    Dial dial() const { return m_dial; }
    double dialAngle() const { return m_dialAngle; }
    QString clockText() const { return m_clockText; }
    QcWorldLabelModel *labels() const { return m_labels; }
    QcDebugHud *debug() const { return m_debug; }
    // Content name under the crosshair ("qtcraft:stone", "mob:qtcraft:pig") for the handbook key.
    QString targetName() const { return m_targetName; }

    // The session's target and mining progress, once per frame.
    void updateTarget();
    void updateTimeOfDay(double alpha);
    // The dial of the held compass or clock, once per frame.
    void updateDial(double alpha);
    void addChatLine(const QString &text);
    // Places the labels near the camera on the view, once per frame (empty while switched off).
    void updateLabels();

signals:
    void frameStateChanged();
    void chatLinesChanged();
    void dialChanged();

private:
    // Names of armour stands that show them, as labels above their heads.
    QList<QcWorldLabel> standLabels() const;
    // The session's world labels (while shown) and the stand names; without stand names no copy.
    const QHash<QcChunkPos, QList<QcWorldLabel>> &labelsToDraw();

    const QcSessionParts &m_parts;
    const QcSettingsController &m_settings;
    double m_miningProgress = 0.0;
    QString m_targetLabel;
    QString m_targetName;
    QString m_timeOfDay;
    QStringList m_chatLines;
    Dial m_dial = Dial::None;
    double m_dialAngle = 0.0;
    QString m_clockText;
    QcWorldLabelModel *m_labels = nullptr;
    // The last merge of labelsToDraw and what it was made from.
    QHash<QcChunkPos, QList<QcWorldLabel>> m_mergedFrom;
    QList<QcWorldLabel> m_standLabels;
    QHash<QcChunkPos, QList<QcWorldLabel>> m_mergedLabels;
    QcDebugHud *m_debug = nullptr;
};

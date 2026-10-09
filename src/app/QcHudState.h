#pragma once

#include "QcVec3.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

class QcSettingsController;
struct QcSessionParts;

// HUD texts that change while playing (GameController.hud): what the crosshair points at with the
// mining progress, the time of day, the debug line and the last chat lines.
class QcHudState : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(double miningProgress READ miningProgress NOTIFY frameStateChanged FINAL)
    Q_PROPERTY(QString targetLabel READ targetLabel NOTIFY frameStateChanged FINAL)
    Q_PROPERTY(QString timeOfDay READ timeOfDay NOTIFY frameStateChanged FINAL)
    Q_PROPERTY(QString debugText READ debugText NOTIFY debugTextChanged FINAL)
    Q_PROPERTY(QStringList chatLines READ chatLines NOTIFY chatLinesChanged FINAL)

public:
    QcHudState(const QcSessionParts &parts, const QcSettingsController &settings, QObject *parent = nullptr);

    double miningProgress() const { return m_miningProgress; }
    QString targetLabel() const { return m_targetLabel; }
    QString timeOfDay() const { return m_timeOfDay; }
    QString debugText() const { return m_debugText; }
    QStringList chatLines() const { return m_chatLines; }
    // Content name under the crosshair ("qtcraft:stone", "mob:qtcraft:pig") for the handbook key.
    QString targetName() const { return m_targetName; }

    // The session's target and mining progress, once per frame.
    void updateTarget();
    void updateTimeOfDay(double alpha);
    // A few times a second while the debug overlay is shown.
    void updateDebugText(const QcVec3 &feet, double fps);
    // The debug overlay was switched: the next frame writes the text at once.
    void resetDebugClock() { m_debugClock.invalidate(); }
    void addChatLine(const QString &text);

signals:
    void frameStateChanged();
    void debugTextChanged();
    void chatLinesChanged();

private:
    static constexpr qint64 debugTextIntervalMs = 250;

    const QcSessionParts &m_parts;
    const QcSettingsController &m_settings;
    double m_miningProgress = 0.0;
    QString m_targetLabel;
    QString m_targetName;
    QString m_timeOfDay;
    QString m_debugText;
    QElapsedTimer m_debugClock;
    QStringList m_chatLines;
};

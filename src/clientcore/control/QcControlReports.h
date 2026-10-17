#pragma once

#include "common/content/definitions/QcAppearance.h"
#include "common/core/QcVec3.h"
#include "common/gameplay/QcItemStack.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>

class QcClientSession;

// JSON views of the session state that control responses report.
class QcControlReports
{
public:
    // `chatLog`: chat lines received while the current request runs.
    QcControlReports(const QcClientSession &session, const QStringList &chatLog);

    static QJsonArray vectorJson(const QcVec3 &v);
    // A player's look with the colours as "#rrggbb".
    static QJsonObject appearanceJson(const QcAppearance &look);

    QJsonObject playerStatus() const;
    QJsonValue stackJson(QcItemStack stack) const;
    QJsonObject inventoryJson() const;
    QJsonObject machineJson() const;
    // Recent sound cues with their loudness and side as heard now, and the ambience levels.
    QJsonObject soundsJson() const;
    // The weather where the player stands (decision E-395).
    QJsonObject weatherJson() const;

private:
    void insertTarget(QJsonObject &status) const;

    const QcClientSession &m_session;
    const QStringList &m_chatLog;
};

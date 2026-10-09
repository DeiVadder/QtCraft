#pragma once

#include "QcItemStack.h"
#include "QcVec3.h"

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

    QJsonObject playerStatus() const;
    QJsonValue stackJson(QcItemStack stack) const;
    QJsonObject inventoryJson() const;
    QJsonObject machineJson() const;
    // Recent sound cues with their loudness and side as heard now, and the ambience levels.
    QJsonObject soundsJson() const;

private:
    void insertTarget(QJsonObject &status) const;

    const QcClientSession &m_session;
    const QStringList &m_chatLog;
};

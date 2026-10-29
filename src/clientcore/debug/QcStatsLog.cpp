// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/debug/QcStatsLog.h"

#include "clientcore/debug/QcClientLogging.h"

#include <QJsonDocument>
#include <QTimer>

#include <algorithm>

using namespace Qt::StringLiterals;

QcStatsLog::QcStatsLog(int intervalMs, Source source, QObject *parent)
    : QObject(parent)
    , m_source(std::move(source))
    , m_timer(new QTimer(this))
{
    m_timer->setInterval(std::clamp(intervalMs, minIntervalMs, maxIntervalMs));
    connect(m_timer, &QTimer::timeout, this, &QcStatsLog::writeLine);
    m_timer->start();
}

int QcStatsLog::interval(const QString &option)
{
    const QString text = option.isEmpty() ? qEnvironmentVariable("QTCRAFT_STATS") : option;
    bool ok = false;
    const int value = text.trimmed().toInt(&ok);
    return ok && value > 0 ? std::clamp(value, minIntervalMs, maxIntervalMs) : 0;
}

int QcStatsLog::intervalMs() const
{
    return m_timer->interval();
}

void QcStatsLog::writeLine()
{
    const std::optional<QJsonObject> data = m_source ? m_source() : std::nullopt;
    if (!data) {
        return;
    }

    const QString json = QString::fromUtf8(QJsonDocument(*data).toJson(QJsonDocument::Compact));
    qCInfo(lcStats).noquote() << u"STATS "_s + json;
    emit logged(json);
}

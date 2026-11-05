// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

class QTimer;

/*! \brief The statistics log (decision E-225): every interval one line "STATS {json}" in the log
    (category qtcraft.stats), with the debug data as QcDebugJson writes it, so scripts and agents
    read the same numbers as the debug display.

    Switched on with --stats <ms> or QTCRAFT_STATS=<ms>. */
class QcStatsLog : public QObject
{
    Q_OBJECT

public:
    /*! Shortest interval taken, in milliseconds. */
    static constexpr int minIntervalMs = 100;
    /*! Longest interval taken, in milliseconds. */
    static constexpr int maxIntervalMs = 3'600'000;
    /*! The data now; std::nullopt while there is none (no world), then nothing is logged. */
    using Source = std::function<std::optional<QJsonObject>()>;

    QcStatsLog(int intervalMs, Source source, QObject *parent = nullptr);

    /*! Returns the interval from the option value \a option or QTCRAFT_STATS ("" uses the
        variable): milliseconds, clamped to the limits; 0 when unset, not a number or not
        positive (log off). */
    static int interval(const QString &option);
    /*! Returns the interval in milliseconds. */
    int intervalMs() const;

signals:
    /*! Emitted with \a json, the line just logged, without the "STATS " prefix. */
    void logged(const QString &json);

private:
    void writeLine();

    Source m_source;
    QTimer *m_timer = nullptr;
};

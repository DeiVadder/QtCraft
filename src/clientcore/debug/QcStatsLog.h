#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

class QTimer;

// The statistics log (decision E-225): every interval one line "STATS {json}" in the log
// (category qtcraft.stats), with the debug data as QcDebugJson writes it, so scripts and agents read
// the same numbers as the debug display. Switched on with --stats <ms> or QTCRAFT_STATS=<ms>.
class QcStatsLog : public QObject
{
    Q_OBJECT

public:
    static constexpr int minIntervalMs = 100;
    static constexpr int maxIntervalMs = 3'600'000;
    // The data now; std::nullopt while there is none (no world), then nothing is logged.
    using Source = std::function<std::optional<QJsonObject>()>;

    QcStatsLog(int intervalMs, Source source, QObject *parent = nullptr);

    // The interval from an option value or QTCRAFT_STATS ("" uses the variable): milliseconds,
    // clamped to the limits; 0 when unset, not a number or not positive (log off).
    static int interval(const QString &option);
    int intervalMs() const;

signals:
    // The line just logged, without the "STATS " prefix.
    void logged(const QString &json);

private:
    void writeLine();

    Source m_source;
    QTimer *m_timer = nullptr;
};

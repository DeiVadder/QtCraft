// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/diagnostics/QcHealthSample.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

class QTimer;

// The health line (decision E-834): every interval one info line "HEALTH …" in qtcraft.health with
// memory (resident and peak), frame and tick times, chunks, entities, players, round trip and
// network rates, so a log of a long playtest shows how the program fared before a problem. The
// source fills what its program knows; memory comes from QcProcessMemory.
class QcHealthLog : public QObject
{
    Q_OBJECT

public:
    using Source = std::function<QcHealthSample()>;
    // Default interval when a log file is written and no --health-log is given.
    static constexpr int defaultIntervalSeconds = 60;

    QcHealthLog(int intervalSeconds, Source source, QObject *parent = nullptr);

    // Memory figures of a line in MiB.
    struct Memory
    {
        double residentMb = 0.0;
        double peakMb = 0.0;
    };
    // The line for `sample`; network rates against `previous` over `seconds`.
    static QString line(const QcHealthSample &sample, Memory memory, const std::optional<QcTraffic> &previous, double seconds);

private:
    void writeLine();

    Source m_source;
    QTimer *m_timer = nullptr;
    std::optional<QcTraffic> m_previousTraffic;
    // Since the previous line: the rates use the time that passed, not the nominal interval.
    QElapsedTimer m_sinceLine;
};

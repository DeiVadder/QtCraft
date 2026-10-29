// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QList>
#include <QString>

#include <optional>

// What the diagnostics options asked for (decision E-834).
struct QcDiagnosticsSetup
{
    // --log-file: this file instead of the session log in <data>/logs.
    std::optional<QString> logFile;
    // --no-log-file: no log file at all (crash reports still go to <data>/logs).
    bool noLogFile = false;
    // --log-rules: logging rules like QT_LOGGING_RULES ("qtcraft.server.debug=true;…").
    QString logRules;
    // --health-log: seconds between health lines, 0 for none; unset: the default of the program.
    std::optional<int> healthSeconds;
    // --full-dump: Windows minidumps with the whole memory.
    bool fullDump = false;
    // --crash-test (hidden test hook): crash this way once the handler is installed.
    QString crashTest;
};

// The command line options of logging, crash reports and the health line, shared by qtcraft and
// qtcraft-headless.
struct QcDiagnosticsOptions
{
    static constexpr int maxHealthSeconds = 24 * 3600;

    QCommandLineOption logFile;
    QCommandLineOption noLogFile;
    QCommandLineOption logRules;
    QCommandLineOption healthLog;
    QCommandLineOption fullDump;
    QCommandLineOption crashTest;

    QcDiagnosticsOptions();
    QList<QCommandLineOption> all() const { return {logFile, noLogFile, logRules, healthLog, fullDump, crashTest}; }
    // The options of a parsed command line; the error names a bad value.
    [[nodiscard]] QcResult<QcDiagnosticsSetup> read(const QCommandLineParser &parser) const;
};

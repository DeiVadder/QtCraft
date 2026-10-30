// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QList>
#include <QString>

#include <optional>

/*! \brief What the diagnostics options asked for (decision E-834). */
struct QcDiagnosticsSetup
{
    /*! --log-file: this file instead of the session log in <data>/logs. */
    std::optional<QString> logFile;
    /*! --no-log-file: no log file at all (crash reports still go to <data>/logs). */
    bool noLogFile = false;
    /*! --log-rules: logging rules like QT_LOGGING_RULES ("qtcraft.server.debug=true;…"). */
    QString logRules;
    /*! --health-log: seconds between health lines, 0 for none; unset: the default of the
        program. */
    std::optional<int> healthSeconds;
    /*! --full-dump: Windows minidumps with the whole memory. */
    bool fullDump = false;
    /*! --crash-test (hidden test hook): crash this way once the handler is installed. */
    QString crashTest;
};

/*! \brief The command line options of logging, crash reports and the health line, shared by
    qtcraft and qtcraft-headless. */
struct QcDiagnosticsOptions
{
    /*! The longest interval --health-log takes: a day. */
    static constexpr int maxHealthSeconds = 24 * 3600;

    /*! --log-file */
    QCommandLineOption logFile;
    /*! --no-log-file */
    QCommandLineOption noLogFile;
    /*! --log-rules */
    QCommandLineOption logRules;
    /*! --health-log */
    QCommandLineOption healthLog;
    /*! --full-dump */
    QCommandLineOption fullDump;
    /*! --crash-test, hidden from the help */
    QCommandLineOption crashTest;

    QcDiagnosticsOptions();
    /*! Returns all six options, to add them to a parser. */
    QList<QCommandLineOption> all() const { return {logFile, noLogFile, logRules, healthLog, fullDump, crashTest}; }
    /*! Returns the options of the parsed command line \a parser; the error names a bad value. */
    [[nodiscard]] QcResult<QcDiagnosticsSetup> read(const QCommandLineParser &parser) const;
};

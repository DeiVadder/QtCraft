// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/diagnostics/QcDiagnosticsOptions.h"

#include "common/diagnostics/QcCrashHandler.h"

#include <QStringList>

using namespace Qt::StringLiterals;

namespace {

QCommandLineOption hidden(QCommandLineOption option)
{
    option.setFlags(QCommandLineOption::HiddenFromHelp);
    return option;
}

} // namespace

QcDiagnosticsOptions::QcDiagnosticsOptions()
    : logFile(u"log-file"_s, u"Write the log to this file (default: a session log in <data>/logs; the game's setting can switch it off)."_s,
              u"file"_s)
    , noLogFile(u"no-log-file"_s, u"Write no log file (crash reports still go to <data>/logs)."_s)
    , logRules(u"log-rules"_s, u"Logging rules like QT_LOGGING_RULES, e.g. 'qtcraft.network.debug=true;qtcraft.server.debug=true'."_s,
               u"rules"_s)
    , healthLog(u"health-log"_s,
                u"Log a health line (memory, frame and tick times, chunks, entities, players, network) every this many seconds; "
                u"0: none (default: 60 while a log file is written)."_s,
                u"seconds"_s)
    , fullDump(u"full-dump"_s, u"Windows: crash dumps with the whole memory of the process (large) instead of the stacks."_s)
    , crashTest(hidden({u"crash-test"_s, u"Test hook: crash at once after the start (segv, abort, fatal, terminate, trap, thread-segv or thread-overflow)."_s, u"kind"_s}))
{
}

QcResult<QcDiagnosticsSetup> QcDiagnosticsOptions::read(const QCommandLineParser &parser) const
{
    QcDiagnosticsSetup setup;
    if (parser.isSet(logFile)) {
        setup.logFile = parser.value(logFile);
        if (setup.logFile->isEmpty()) {
            return qcError(u"--log-file needs a file name"_s);
        }
    }
    setup.noLogFile = parser.isSet(noLogFile);
    setup.logRules = parser.value(logRules);
    if (parser.isSet(healthLog)) {
        bool ok = false;
        const int seconds = parser.value(healthLog).toInt(&ok);
        if (!ok || seconds < 0 || seconds > maxHealthSeconds) {
            return qcError(u"--health-log takes seconds from 0 to %1"_s.arg(maxHealthSeconds));
        }
        setup.healthSeconds = seconds;
    }
    setup.fullDump = parser.isSet(fullDump);
    setup.crashTest = parser.value(crashTest);
    if (!setup.crashTest.isEmpty() && !QcCrashHandler::crashTestKinds().contains(setup.crashTest)) {
        return qcError(u"Unknown --crash-test '%1'"_s.arg(setup.crashTest));
    }
    return setup;
}

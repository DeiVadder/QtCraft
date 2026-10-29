// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/diagnostics/QcDiagnosticsOptions.h"

#include <QString>
#include <QStringList>

// Logging and crash reports of one run of a program (decision E-834), from the start of main()
// until its end: deletes the files of old sessions, installs the log file (QcMessageLog) with its
// header and the crash handler, applies --log-rules, and on destruction closes the log. Files go to
// <data>/logs unless --log-file names another file.
class QcDiagnosticsSession
{
public:
    struct Start
    {
        // "qtcraft" or "qtcraft-headless": the first part of the file names.
        QString program;
        // The data root of the run (QcDataRoot or --data-dir).
        QString dataDirectory;
        // Whether the program writes a session log without --log-file (the game unless its
        // setting says no; the dedicated server).
        bool logByDefault = false;
        QcDiagnosticsSetup setup;
        // The command line, written to the header with the values of --password hidden.
        QStringList arguments;
    };

    explicit QcDiagnosticsSession(const Start &start);
    ~QcDiagnosticsSession();
    Q_DISABLE_COPY_MOVE(QcDiagnosticsSession)

    // <data>/logs.
    QString logDirectory() const { return m_logDirectory; }
    // The log written now; empty when none.
    QString logPath() const { return m_logPath; }
    // This run's crash report (written only on a crash).
    QString crashReportPath() const { return m_crashReport; }
    // The crash report the previous session of this program left (renamed by QcLogFiles::markSeen);
    // empty when it ended normally or an earlier start named it already.
    QString previousCrashReport() const { return m_previousCrash; }
    // Seconds between health lines: --health-log, else the default while a log file is written.
    int healthSeconds() const { return m_healthSeconds; }
    // Runs --crash-test when it was given (does not return then).
    void runCrashTest() const;

    // The command line with the value of every password option replaced (header, bundle).
    static QStringList withoutSecrets(const QStringList &arguments);

private:
    void openLog(const Start &start, const QString &session);
    void installCrashHandler(const Start &start, const QString &session) const;

    QString m_logDirectory;
    QString m_logPath;
    QString m_crashReport;
    QString m_previousCrash;
    QString m_crashTest;
    int m_healthSeconds = 0;
};

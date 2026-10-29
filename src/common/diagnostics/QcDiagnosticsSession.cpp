// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/diagnostics/QcDiagnosticsSession.h"

#include "common/core/QcLogging.h"
#include "common/diagnostics/QcBuildInfo.h"
#include "common/diagnostics/QcCrashHandler.h"
#include "common/diagnostics/QcHealthLog.h"
#include "common/diagnostics/QcLogFiles.h"
#include "common/diagnostics/QcMessageLog.h"
#include "common/protocol/QcProtocol.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>

using namespace Qt::StringLiterals;

QcDiagnosticsSession::QcDiagnosticsSession(const Start &start)
    : m_logDirectory(QDir(start.dataDirectory).filePath(QString::fromLatin1(QcLogFiles::folderName)))
    , m_crashTest(start.setup.crashTest)
{
    // Before the first message, so the file sees them all.
    if (!start.setup.logRules.isEmpty()) {
        QLoggingCategory::setFilterRules(QString(start.setup.logRules).replace(u';', u'\n'));
    }

    QDir().mkpath(m_logDirectory);
    const QDir folder(m_logDirectory);
    const QString session = QcLogFiles::freeSessionName(folder, start.program, QDateTime::currentDateTimeUtc());
    if (const QString crash = QcLogFiles::previousCrashReport(folder, start.program, session); !crash.isEmpty()) {
        m_previousCrash = QcLogFiles::markSeen(crash);
    }
    QcLogFiles::prune(folder, start.program, QcLogFiles::defaultKeptSessions - 1);
    m_crashReport = folder.absoluteFilePath(QcLogFiles::crashReportName(session));

    openLog(start, session);
    installCrashHandler(start, session);
    m_healthSeconds = start.setup.healthSeconds.value_or(m_logPath.isEmpty() ? 0 : QcHealthLog::defaultIntervalSeconds);
    if (!m_previousCrash.isEmpty()) {
        qCWarning(lcDiagnostics).noquote() << u"The previous session crashed; its report: %1"_s.arg(m_previousCrash);
    }
}

QcDiagnosticsSession::~QcDiagnosticsSession()
{
    if (!m_logPath.isEmpty()) {
        qCInfo(lcDiagnostics, "Session ended");
    }
    QcMessageLog::uninstall();
}

void QcDiagnosticsSession::openLog(const Start &start, const QString &session)
{
    // Without a file the handler still gives a fatal message to the crash report.
    QcMessageLog::installWithoutFile();
    const bool wanted = start.setup.logFile.has_value() || (start.logByDefault && !start.setup.noLogFile);
    if (!wanted) {
        return;
    }

    const QString path = start.setup.logFile ? QFileInfo(*start.setup.logFile).absoluteFilePath() : QDir(m_logDirectory).absoluteFilePath(QcLogFiles::logName(session));
    QStringList header = QcBuildInfo::headerLines(start.program);
    header.append(u"Protocol %1, data folder %2"_s.arg(QcProtocol::version).arg(QDir::toNativeSeparators(QDir(start.dataDirectory).absolutePath())));
    header.append(u"Command line: "_s + withoutSecrets(start.arguments).join(u' '));
    header.append(u"Crash report on a crash: "_s + QDir::toNativeSeparators(m_crashReport));
    if (const QcStatus installed = QcMessageLog::install({path, {}, header}); !installed) {
        qCWarning(lcDiagnostics).noquote() << u"No log file:"_s << installed.error();
        return;
    }
    m_logPath = path;
}

void QcDiagnosticsSession::installCrashHandler(const Start &start, const QString &session) const
{
    QByteArray header = u"QtCraft crash report\n"_s.toUtf8();
    const QStringList lines = QcBuildInfo::headerLines(start.program);
    for (const QString &line : lines) {
        header += line.toUtf8() + '\n';
    }
    header += u"Log: %1\n"_s.arg(QDir::toNativeSeparators(m_logPath)).toUtf8();
    const QString dump = QDir(m_logDirectory).filePath(QcLogFiles::dumpName(session));
    QcCrashHandler::install({m_crashReport, dump, start.setup.fullDump, header});
}

void QcDiagnosticsSession::runCrashTest() const
{
    if (!m_crashTest.isEmpty()) {
        qCWarning(lcDiagnostics).noquote() << u"Crashing on purpose (--crash-test %1)"_s.arg(m_crashTest);
        QcCrashHandler::crashForTest(m_crashTest);
    }
}

QStringList QcDiagnosticsSession::withoutSecrets(const QStringList &arguments)
{
    QStringList shown;
    bool hideNext = false;
    for (const QString &argument : arguments) {
        if (hideNext) {
            shown.append(u"<hidden>"_s);
            hideNext = false;
        } else if (argument.startsWith(u"--password="_s) || argument.startsWith(u"-password="_s)) {
            shown.append(argument.section(u'=', 0, 0) + u"=<hidden>"_s);
        } else {
            hideNext = argument == u"--password"_s || argument == u"-password"_s;
            shown.append(argument);
        }
    }
    return shown;
}

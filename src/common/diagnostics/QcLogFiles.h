// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDateTime>
#include <QDir>
#include <QString>
#include <QStringList>

/*! \brief The files of the diagnostics folder (decision E-834): one session per start of a program,
    named "<program>-<yyyyMMdd-HHmmss>" in UTC (so the order survives a change of daylight saving
    time; the lines inside carry local time), with "-2", "-3" … when that name is taken.

    A session's files share its name: the log "<session>.log", its rotated parts
    "<session>.1.log" …, a crash report "<session>.crash.txt" (renamed to
    "<session>.crash-seen.txt" once a later start named it) and on Windows a memory dump
    "<session>.dmp". */
namespace QcLogFiles {

/*! The folder below the data root. */
inline constexpr char folderName[] = "logs";
/*! Sessions kept per program; older ones are deleted at the start of a new one. */
inline constexpr int defaultKeptSessions = 10;

/*! Returns the log's file name of \a session: "<session>.log". */
QString logName(const QString &session);
/*! Returns the crash report's file name of \a session: "<session>.crash.txt". */
QString crashReportName(const QString &session);
/*! Returns the file name of the crash report of \a session once a later start named it:
    "<session>.crash-seen.txt". */
QString seenCrashReportName(const QString &session);
/*! Returns the memory dump's file name of \a session: "<session>.dmp". */
QString dumpName(const QString &session);
/*! Returns the rotated part \a part (1 = the newest) of the log at \a logPath:
    "<dir>/<session>.<part>.log". */
QString rotatedPath(const QString &logPath, int part);

/*! A session name for \a program started at \a start that no file in \a dir uses yet. */
QString freeSessionName(const QDir &dir, const QString &program, const QDateTime &start);
/*! Returns the sessions of \a program with files in \a dir, oldest first. */
QStringList sessions(const QDir &dir, const QString &program);
/*! Deletes the files in \a dir of all but the newest \a keep sessions of \a program; returns the
    deleted file names. */
QStringList prune(const QDir &dir, const QString &program, int keep);
/*! Returns the path of the crash report in \a dir of the newest session of \a program before
    \a current that no start named yet; empty when that session ended without one. */
QString previousCrashReport(const QDir &dir, const QString &program, const QString &current);
/*! Returns the files of \a dir a diagnostics bundle carries: those of the newest
    \a sessionsPerProgram sessions of every program and every crash report and dump, sorted by
    name. */
QStringList bundleFiles(const QDir &dir, int sessionsPerProgram);
/*! Renames the crash report \a reportPath found by previousCrashReport() so later starts do not
    name it again (sessions without a log leave no file of their own); returns the new path, or
    the old one when the rename failed. */
QString markSeen(const QString &reportPath);

} // namespace QcLogFiles

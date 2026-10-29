// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDateTime>
#include <QDir>
#include <QString>
#include <QStringList>

// The files of the diagnostics folder (decision E-834): one session per start of a program, named
// "<program>-<yyyyMMdd-HHmmss>" in UTC (so the order survives a change of daylight saving time; the
// lines inside carry local time), with "-2", "-3" … when that name is taken. A session's files
// share its name: the log "<session>.log", its rotated parts "<session>.1.log" …, a crash report
// "<session>.crash.txt" (renamed to "<session>.crash-seen.txt" once a later start named it) and on
// Windows a memory dump "<session>.dmp".
namespace QcLogFiles {

// The folder below the data root.
inline constexpr char folderName[] = "logs";
// Sessions kept per program; older ones are deleted at the start of a new one.
inline constexpr int defaultKeptSessions = 10;

QString logName(const QString &session);
QString crashReportName(const QString &session);
QString seenCrashReportName(const QString &session);
QString dumpName(const QString &session);
// The rotated part `part` (1 = the newest) of the log at `logPath`: "<dir>/<session>.<part>.log".
QString rotatedPath(const QString &logPath, int part);

// A session name for `program` started at `start` that no file in `dir` uses yet.
QString freeSessionName(const QDir &dir, const QString &program, const QDateTime &start);
// The sessions of `program` with files in `dir`, oldest first.
QStringList sessions(const QDir &dir, const QString &program);
// Deletes the files of all but the newest `keep` sessions of `program`; returns the deleted file
// names.
QStringList prune(const QDir &dir, const QString &program, int keep);
// The path of the crash report of the newest session of `program` before `current` that no start
// named yet; empty when that session ended without one.
QString previousCrashReport(const QDir &dir, const QString &program, const QString &current);
// The files of `dir` a diagnostics bundle carries: those of the newest `sessionsPerProgram`
// sessions of every program and every crash report and dump, sorted by name.
QStringList bundleFiles(const QDir &dir, int sessionsPerProgram);
// Renames a crash report found by previousCrashReport() so later starts do not name it again
// (sessions without a log leave no file of their own); returns the new path, or the old one when
// the rename failed.
QString markSeen(const QString &reportPath);

} // namespace QcLogFiles

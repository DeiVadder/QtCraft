// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/diagnostics/QcLogFiles.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

// The session a file of `program` belongs to: the program, date, time and an optional counter.
QRegularExpression sessionPattern(const QString &program)
{
    return QRegularExpression(u"^(%1-\\d{8}-\\d{6}(?:-\\d+)?)\\."_s.arg(QRegularExpression::escape(program)));
}

// The session of a file name; empty for a file of no session of `program`.
QString sessionOf(const QString &fileName, const QRegularExpression &pattern)
{
    const QRegularExpressionMatch match = pattern.match(fileName);
    return match.hasMatch() ? match.captured(1) : QString();
}

// The program a file of any session belongs to (captured 1).
const QRegularExpression &anySessionPattern()
{
    static const QRegularExpression pattern(u"^(.+)-\\d{8}-\\d{6}(?:-\\d+)?\\."_s);
    return pattern;
}

bool isCrashFile(const QString &name)
{
    return name.endsWith(u".crash.txt"_s) || name.endsWith(u".crash-seen.txt"_s) || name.endsWith(u".dmp"_s);
}

// Length of "-yyyyMMdd-HHmmss".
constexpr qsizetype stampLength = 16;

// Sessions sort by start time, then by the counter of a later session in the same second.
std::pair<QString, int> startKey(const QString &session, qsizetype programLength)
{
    const qsizetype stampEnd = programLength + stampLength;
    const int counter = session.size() > stampEnd ? QStringView(session).mid(stampEnd + 1).toInt() : 1;
    return {session.left(stampEnd), counter};
}

} // namespace

namespace QcLogFiles {

QString logName(const QString &session)
{
    return session + u".log"_s;
}

QString crashReportName(const QString &session)
{
    return session + u".crash.txt"_s;
}

QString seenCrashReportName(const QString &session)
{
    return session + u".crash-seen.txt"_s;
}

QString dumpName(const QString &session)
{
    return session + u".dmp"_s;
}

QString rotatedPath(const QString &logPath, int part)
{
    const QFileInfo info(logPath);
    const QString base = info.fileName().endsWith(u".log"_s) ? info.fileName().chopped(4) : info.fileName();
    return info.dir().filePath(u"%1.%2.log"_s.arg(base).arg(part));
}

QString freeSessionName(const QDir &dir, const QString &program, const QDateTime &start)
{
    const QString base = u"%1-%2"_s.arg(program, start.toString(u"yyyyMMdd-HHmmss"_s));
    const QStringList taken = sessions(dir, program);
    QString name = base;
    for (int counter = 2; taken.contains(name); ++counter) {
        name = u"%1-%2"_s.arg(base).arg(counter);
    }
    return name;
}

QStringList sessions(const QDir &dir, const QString &program)
{
    const QRegularExpression pattern = sessionPattern(program);
    QStringList found;
    const QStringList names = dir.entryList(QDir::Files);
    for (const QString &name : names) {
        if (const QString session = sessionOf(name, pattern); !session.isEmpty() && !found.contains(session)) {
            found.append(session);
        }
    }
    std::ranges::sort(found, {}, [&program](const QString &session) { return startKey(session, program.size()); });
    return found;
}

QStringList prune(const QDir &dir, const QString &program, int keep)
{
    const QStringList all = sessions(dir, program);
    const QStringList old = all.mid(0, std::max<qsizetype>(0, all.size() - std::max(keep, 0)));
    const QRegularExpression pattern = sessionPattern(program);
    QStringList deleted;
    const QStringList names = dir.entryList(QDir::Files);
    for (const QString &name : names) {
        if (old.contains(sessionOf(name, pattern)) && QFile::remove(dir.filePath(name))) {
            deleted.append(name);
        }
    }
    return deleted;
}

QString previousCrashReport(const QDir &dir, const QString &program, const QString &current)
{
    QStringList earlier = sessions(dir, program);
    earlier.removeAll(current);
    const auto key = [&program](const QString &session) { return startKey(session, program.size()); };
    earlier.removeIf([&](const QString &session) { return key(session) >= key(current); });
    if (earlier.isEmpty()) {
        return {};
    }
    const QString report = dir.filePath(crashReportName(earlier.constLast()));
    return QFileInfo::exists(report) ? report : QString();
}

QStringList bundleFiles(const QDir &dir, int sessionsPerProgram)
{
    const QStringList names = dir.entryList(QDir::Files, QDir::Name);
    QStringList programs;
    for (const QString &name : names) {
        if (const QRegularExpressionMatch match = anySessionPattern().match(name); match.hasMatch() && !programs.contains(match.captured(1))) {
            programs.append(match.captured(1));
        }
    }
    QStringList recent;
    for (const QString &program : std::as_const(programs)) {
        const QStringList all = sessions(dir, program);
        recent += all.mid(std::max<qsizetype>(0, all.size() - sessionsPerProgram));
    }

    QStringList chosen;
    for (const QString &name : names) {
        const bool inRecent = std::ranges::any_of(recent, [&name](const QString &session) { return name.startsWith(session + u'.'); });
        if (inRecent || isCrashFile(name)) {
            chosen.append(name);
        }
    }
    return chosen;
}

QString markSeen(const QString &reportPath)
{
    const QFileInfo info(reportPath);
    const QString suffix = u".crash.txt"_s;
    if (!info.fileName().endsWith(suffix)) {
        return reportPath;
    }
    const QString seen = info.dir().filePath(seenCrashReportName(info.fileName().chopped(suffix.size())));
    QFile::remove(seen);
    return QFile::rename(reportPath, seen) ? seen : reportPath;
}

} // namespace QcLogFiles

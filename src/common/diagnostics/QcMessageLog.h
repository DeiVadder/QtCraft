#pragma once

#include "common/core/QcResult.h"
#include "common/diagnostics/QcLogWriter.h"

#include <QDateTime>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QtLogging>

// The log file of a program (decision E-834): a Qt message handler that writes every message that
// passes the logging rules to a QcLogWriter and then hands it on to the handler installed before
// (the console). Each line carries the absolute time and the seconds since the start; warnings and
// worse land at once (the file is unbuffered), a fatal message also becomes the reason of the crash
// report. Thread-safe: messages from any thread are written whole under one mutex; a message the
// writer itself causes goes only to the previous handler.
namespace QcMessageLog {

struct Config
{
    QString path;
    QcLogWriter::Limits limits;
    // Written first, each as an info line of qtcraft.diagnostics, to the file only and whatever the
    // logging rules say (note()).
    QStringList header;
};

// Installs the handler (once; a second call replaces the file). The error says why the file cannot
// be written; then nothing is installed.
[[nodiscard]] QcStatus install(const Config &config);
// Installs only the handler, without a file: a fatal message still becomes the reason of the
// crash report. install() later adds the file.
void installWithoutFile();
// Restores the previous handler and closes the file; safe without install().
void uninstall();
// The file written now; empty when none.
QString path();
// Writes `text` as an info line of `category` to the file only, not on to the console (lines a
// program prints to standard output itself).
void note(const char *category, const QString &text);

// When a line was written: the local time and the milliseconds since the log started.
struct LineTime
{
    QDateTime now;
    qint64 sinceStartMs = 0;
};

// The latest lines that passed the logging rules, kept in memory while the handler is installed
// (with or without a file) for the admin channel's log view (decision E-848): at most recentLimit
// lines of at most recentTextLimit characters, numbered from 1 in the order they came.
inline constexpr qsizetype recentLimit = 2000;
inline constexpr qsizetype recentTextLimit = 1024;
struct RecentLine
{
    quint64 sequence = 0;
    // Milliseconds since the epoch (UTC).
    qint64 timeMs = 0;
    // 0 debug, 1 info, 2 warning, 3 critical, 4 fatal (severity()).
    int severity = 1;
    QString category;
    QString text;
};
// The order of the message types: debug < info < warning < critical < fatal.
int severity(QtMsgType type);
// What recent() found: the lines, and the sequence of the newest line kept when it looked (read
// under the same lock, so a caller that asks again after it gets every later line exactly once).
struct Recent
{
    QList<RecentLine> lines;
    quint64 latest = 0;
};
// The newest lines of at least `minimumSeverity` with a sequence above `after`, oldest first, at
// most `count`; with `categories` only lines of those categories.
[[nodiscard]] Recent recent(int minimumSeverity, qsizetype count, quint64 after = 0, const QSet<QString> &categories = {});
// The sequence of the newest line kept; 0 before the first.
quint64 latestSequence();

// One line of the file: "<date time> +<seconds> <D|I|W|C|F> <category> [<thread>] <message>".
QByteArray formatLine(QtMsgType type, const char *category, const QString &message, const LineTime &time, const QString &thread);

} // namespace QcMessageLog

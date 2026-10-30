// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"
#include "common/diagnostics/QcLogWriter.h"

#include <QDateTime>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QtLogging>

/*! \brief The log file of a program (decision E-834): a Qt message handler that writes every
    message that passes the logging rules to a QcLogWriter and then hands it on to the handler
    installed before (the console).

    Each line carries the absolute time and the seconds since the start; warnings and worse land
    at once (the file is unbuffered), a fatal message also becomes the reason of the crash
    report. Thread-safe: messages from any thread are written whole under one mutex; a message
    the writer itself causes goes only to the previous handler. */
namespace QcMessageLog {

/*! \brief The log file install() writes. */
struct Config
{
    /*! The file's path. */
    QString path;
    /*! Its size cap and rotations. */
    QcLogWriter::Limits limits;
    /*! Written first, each as an info line of qtcraft.diagnostics, to the file only and whatever
        the logging rules say (note()). */
    QStringList header;
};

/*! Installs the handler with the file of \a config (once; a second call replaces the file). The
    error says why the file cannot be written; then nothing is installed. */
[[nodiscard]] QcStatus install(const Config &config);
/*! Installs only the handler, without a file: a fatal message still becomes the reason of the
    crash report. install() later adds the file. */
void installWithoutFile();
/*! Restores the previous handler and closes the file; safe without install(). */
void uninstall();
/*! Returns the file written now; empty when none. */
QString path();
/*! Writes \a text as an info line of \a category to the file only, not on to the console (lines
    a program prints to standard output itself). */
void note(const char *category, const QString &text);

/*! \brief When a line was written: the local time and the milliseconds since the log started. */
struct LineTime
{
    /*! The local time. */
    QDateTime now;
    /*! Milliseconds since the log started. */
    qint64 sinceStartMs = 0;
};

/*! The latest lines that passed the logging rules, kept in memory while the handler is installed
    (with or without a file) for the admin channel's log view (decision E-848): at most
    recentLimit lines of at most recentTextLimit characters, numbered from 1 in the order they
    came. \qcshared */
inline constexpr qsizetype recentLimit = 2000;
inline constexpr qsizetype recentTextLimit = 1024;
/*! \brief One of the latest lines. */
struct RecentLine
{
    /*! Its number, from 1 in the order the lines came. */
    quint64 sequence = 0;
    /*! Milliseconds since the epoch (UTC). */
    qint64 timeMs = 0;
    /*! 0 debug, 1 info, 2 warning, 3 critical, 4 fatal (severity()). */
    int severity = 1;
    /*! The logging category. */
    QString category;
    /*! The message. */
    QString text;
};
/*! Returns the severity of \a type, in the order debug < info < warning < critical < fatal
    (0 to 4). */
int severity(QtMsgType type);
/*! \brief What recent() found: the lines, and the sequence of the newest line kept when it
    looked (read under the same lock, so a caller that asks again after it gets every later line
    exactly once). */
struct Recent
{
    /*! The lines, oldest first. */
    QList<RecentLine> lines;
    /*! The sequence of the newest line kept. */
    quint64 latest = 0;
};
/*! Returns the newest lines of at least \a minimumSeverity with a sequence above \a after,
    oldest first, at most \a count; with \a categories only lines of those categories. */
[[nodiscard]] Recent recent(int minimumSeverity, qsizetype count, quint64 after = 0, const QSet<QString> &categories = {});
/*! Returns the sequence of the newest line kept; 0 before the first. */
quint64 latestSequence();

/*! Returns one line of the file for \a message of \a type in \a category, written at \a time on
    \a thread: "<date time> +<seconds> <D|I|W|C|F> <category> [<thread>] <message>". */
QByteArray formatLine(QtMsgType type, const char *category, const QString &message, const LineTime &time, const QString &thread);

} // namespace QcMessageLog

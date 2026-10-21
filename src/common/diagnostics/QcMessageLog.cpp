#include "common/diagnostics/QcMessageLog.h"

#include "common/core/QcLogging.h"
#include "common/diagnostics/QcCrashHandler.h"

#include <QElapsedTimer>
#include <QMutex>
#include <QScopedValueRollback>
#include <QThread>

#include <atomic>
#include <memory>

using namespace Qt::StringLiterals;

namespace {

struct State
{
    QMutex mutex;
    std::unique_ptr<QcLogWriter> writer;
    // Atomic: handlers on other threads read it while install() or uninstall() run.
    std::atomic<QtMessageHandler> previous{nullptr};
    bool installed = false;
    QElapsedTimer started;
};

State &state()
{
    // Never destroyed: messages may come from threads that outlive main().
    static auto *instance = new State;
    return *instance;
}

// Set while this thread writes: a message the writing causes must not wait for its own mutex.
thread_local bool t_writing = false;

char levelLetter(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return 'D';
    case QtInfoMsg:
        return 'I';
    case QtWarningMsg:
        return 'W';
    case QtCriticalMsg:
        return 'C';
    case QtFatalMsg:
        return 'F';
    }
    return '?';
}

// The thread's name where it has one and its id as crash reports name it (QcCrashHandler::threadId).
QString threadName()
{
    const QThread *thread = QThread::currentThread();
    const QString name = thread ? thread->objectName() : QString();
    const QString id = QString::number(QcCrashHandler::threadId());
    return name.isEmpty() ? id : u"%1 %2"_s.arg(name, id);
}

// Writes a line of `type`; the caller holds the mutex and a writer exists. While the file rotates
// its handle is closed (and may be reused by another file), so the crash handler gets none.
void writeLocked(State &log, QtMsgType type, const char *category, const QString &message)
{
    // A message the writing causes must not wait for the mutex this thread holds.
    const QScopedValueRollback writing(t_writing, true);
    const QByteArray line = QcMessageLog::formatLine(type, category, message, {QDateTime::currentDateTime(), log.started.elapsed()}, threadName());
    if (log.writer->rotatesFor(line.size())) {
        QcCrashHandler::setLogHandle(-1);
    }
    log.writer->write(line);
    QcCrashHandler::setLogHandle(log.writer->handle());
}

void writeToFile(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    State &log = state();
    const QMutexLocker lock(&log.mutex);
    if (log.writer) {
        writeLocked(log, type, context.category, message);
    }
}

void handle(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (!t_writing) {
        writeToFile(type, context, message);
    }
    if (type == QtFatalMsg) {
        QcCrashHandler::setReason(message.toUtf8());
        // Windows: Qt ends a fatal message with a fast fail that no handler sees; report it now.
        QcCrashHandler::reportFatalMessage();
    }

    if (const QtMessageHandler previous = state().previous.load()) {
        previous(type, context, message);
    }
}

} // namespace

namespace QcMessageLog {

void installWithoutFile()
{
    State &log = state();
    const QMutexLocker lock(&log.mutex);
    if (!log.installed) {
        log.started.start();
        log.previous.store(qInstallMessageHandler(handle));
        log.installed = true;
    }
}

QcStatus install(const Config &config)
{
    auto writer = std::make_unique<QcLogWriter>(config.path, config.limits);
    if (const QcStatus opened = writer->open(); !opened) {
        return opened;
    }

    installWithoutFile();
    State &log = state();
    {
        const QMutexLocker lock(&log.mutex);
        log.writer = std::move(writer);
        QcCrashHandler::setLogHandle(log.writer->handle());
    }
    // Past the logging rules: a rule that silences the category must not drop the header.
    for (const QString &line : config.header) {
        note(lcDiagnostics().categoryName(), line);
    }
    return {};
}

void uninstall()
{
    State &log = state();
    const QMutexLocker lock(&log.mutex);
    if (log.installed) {
        // A handler installed after this one stays.
        if (const QtMessageHandler current = qInstallMessageHandler(log.previous.load()); current != handle) {
            qInstallMessageHandler(current);
        }
        log.installed = false;
    }
    QcCrashHandler::setLogHandle(-1);
    log.writer.reset();
}

QString path()
{
    State &log = state();
    const QMutexLocker lock(&log.mutex);
    return log.writer ? log.writer->path() : QString();
}

void note(const char *category, const QString &text)
{
    State &log = state();
    const QMutexLocker lock(&log.mutex);
    if (log.writer) {
        writeLocked(log, QtInfoMsg, category, text);
    }
}

QByteArray formatLine(QtMsgType type, const char *category, const QString &message, const LineTime &time, const QString &thread)
{
    const QString now = time.now.toString(u"yyyy-MM-dd HH:mm:ss.zzz"_s);
    const QString since = QString::number(double(time.sinceStartMs) / 1000.0, 'f', 3);
    const QString level(QLatin1Char(levelLetter(type)));
    const QString name = QString::fromLatin1(category ? category : "default");
    return u"%1 +%2 %3 %4 [%5] %6\n"_s.arg(now, since, level, name, thread, message).toUtf8();
}

} // namespace QcMessageLog

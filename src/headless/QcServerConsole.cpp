#include "headless/QcServerConsole.h"

#include "headless/QcStdinReader.h"

#include <cstdio>
#include <print>

QcServerConsole::QcServerConsole(Runner runner, bool stopAtEndOfInput, QObject *parent)
    : QObject(parent)
    , m_runner(std::move(runner))
    , m_reader(new QcStdinReader(this))
    , m_stopAtEndOfInput(stopAtEndOfInput)
{
    connect(m_reader, &QcStdinReader::lineReceived, this, &QcServerConsole::handle);
    connect(m_reader, &QcStdinReader::endOfInput, this, &QcServerConsole::endOfInput);
}

void QcServerConsole::endOfInput()
{
    if (m_stopAtEndOfInput) {
        emit finished();
        return;
    }

    std::println("Standard input closed; the server keeps running.");
    std::fflush(stdout);
}

void QcServerConsole::start()
{
    std::println("QtCraft server console: type a command (help lists them), stop ends the server.");
    std::fflush(stdout);
    m_reader->start();
}

void QcServerConsole::handle(const QByteArray &line)
{
    const QString text = QString::fromUtf8(line).trimmed();
    if (text.isEmpty()) {
        return;
    }

    const QStringList replies = m_runner(text);
    for (const QString &reply : replies) {
        std::println("{}", reply.toStdString());
    }
    std::fflush(stdout);
}

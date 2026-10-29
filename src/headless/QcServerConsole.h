// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QStringList>

#include <functional>

class QcStdinReader;

// The server console of qtcraft-headless --console (decision E-247), a step towards the dedicated
// server: every line of standard input is a server command (the leading '/' is optional), its
// replies go to standard output as English text. /stop or the end of the input ends the process
// (the world is saved); a service without input (--no-eof-stop) keeps running at the end of the
// input. The commands run in the server's thread; this object only hands lines over.
class QcServerConsole : public QObject
{
    Q_OBJECT

public:
    // Runs one console line and returns the replies (QcLocalServerHost::runConsoleCommand).
    using Runner = std::function<QStringList(const QString &line)>;

    QcServerConsole(Runner runner, bool stopAtEndOfInput, QObject *parent = nullptr);

    void start();

signals:
    // End of input (when it stops the server): the owner stops the server.
    void finished();

private:
    void handle(const QByteArray &line);
    void endOfInput();

    Runner m_runner;
    QcStdinReader *m_reader = nullptr;
    bool m_stopAtEndOfInput = true;
};

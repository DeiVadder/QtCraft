// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>

#include <memory>

/*! \brief Reads standard input line by line on a background thread and delivers the lines to the
    owner's thread.

    Blocking reads keep the event loop free and work the same on every platform (a socket
    notifier on stdin does not work on Windows). */
class QcStdinReader : public QObject
{
    Q_OBJECT

public:
    explicit QcStdinReader(QObject *parent = nullptr);
    ~QcStdinReader() override;
    Q_DISABLE_COPY_MOVE(QcStdinReader)

    /*! Starts the reading thread; it is not joined and ends with the process. */
    void start();

signals:
    /*! A \a line of standard input arrived, with its line end (the last line may have none). */
    void lineReceived(const QByteArray &line);
    /*! Standard input has ended; after the last lineReceived(). */
    void endOfInput();

private:
    struct Shared;

    void deliver();

    std::shared_ptr<Shared> m_shared;
};

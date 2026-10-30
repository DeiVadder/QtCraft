// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>

#include <memory>

class QSocketNotifier;

/*! \brief Turns the requests to end the process from outside (SIGTERM from a service manager or
    kill, SIGINT from Ctrl+C; on Windows the console's close and Ctrl+C events) into the signal
    stopRequested() in the event loop, so a server saves before it ends (decision E-255).

    After the first request the default handling is back: a second Ctrl+C ends the process at
    once. One instance per process. */
class QcSignalWatcher : public QObject
{
    Q_OBJECT

public:
    explicit QcSignalWatcher(QObject *parent = nullptr);
    ~QcSignalWatcher() override;
    Q_DISABLE_COPY_MOVE(QcSignalWatcher)

    /*! Returns whether the handlers are installed (false when another watcher exists or the
        system refused). */
    bool isWatching() const { return m_watching; }

signals:
    /*! \a name is the request, e.g. "SIGTERM". */
    void stopRequested(const QString &name);

private:
    void onRequest();

    std::unique_ptr<QSocketNotifier> m_notifier;
    bool m_watching = false;
};

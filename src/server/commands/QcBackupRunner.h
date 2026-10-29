// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcMailbox.h"
#include "server/commands/QcWorldBackup.h"

#include <functional>
#include <thread>

// Copies world snapshots in a background thread (decision E-256), one at a time, so /backup does
// not hold up the ticks. The result comes back to the owner's thread in poll(), which the server
// calls every tick; the hand-over is a mutex-guarded mailbox (decision E-010).
class QcBackupRunner
{
public:
    using Done = std::function<void(const QcResult<QcWorldBackup::Summary> &result)>;

    QcBackupRunner() = default;
    QcBackupRunner(const QcBackupRunner &) = delete;
    QcBackupRunner &operator=(const QcBackupRunner &) = delete;
    // Waits for a running copy (its report is dropped).
    ~QcBackupRunner();

    bool running() const { return m_thread.joinable(); }
    // Copies the snapshot in the background; `done` runs in poll() or wait() afterwards.
    void start(QcWorldBackup::Snapshot snapshot, Done done);
    // Reports a finished copy.
    void poll();
    // Blocks until a running copy has finished and reports it.
    void wait();
    // Test hook: runs in the background thread before the copy starts.
    void setBeforeCopy(std::function<void()> hook) { m_beforeCopy = std::move(hook); }

private:
    void report(const QcResult<QcWorldBackup::Summary> &result);

    std::thread m_thread;
    QcMailbox<QcResult<QcWorldBackup::Summary>> m_results;
    Done m_done;
    std::function<void()> m_beforeCopy;
};

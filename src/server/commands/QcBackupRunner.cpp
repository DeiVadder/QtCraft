#include "QcBackupRunner.h"

QcBackupRunner::~QcBackupRunner()
{
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void QcBackupRunner::start(QcWorldBackup::Snapshot snapshot, Done done)
{
    Q_ASSERT(!running());
    m_done = std::move(done);
    m_thread = std::thread([this, snapshot = std::move(snapshot), beforeCopy = m_beforeCopy] {
        if (beforeCopy) {
            beforeCopy();
        }
        m_results.push(QcWorldBackup::complete(snapshot));
    });
}

void QcBackupRunner::poll()
{
    std::vector<QcResult<QcWorldBackup::Summary>> results = m_results.takeAll();
    if (results.empty()) {
        return;
    }

    m_thread.join();
    report(results.front());
}

void QcBackupRunner::wait()
{
    if (!m_thread.joinable()) {
        return;
    }

    m_thread.join();
    std::vector<QcResult<QcWorldBackup::Summary>> results = m_results.takeAll();
    if (!results.empty()) {
        report(results.front());
    }
}

void QcBackupRunner::report(const QcResult<QcWorldBackup::Summary> &result)
{
    const Done done = std::exchange(m_done, {});
    if (done) {
        done(result);
    }
}

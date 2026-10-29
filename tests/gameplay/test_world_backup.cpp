// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "QcServerTestFixture.h"
#include "server/backup/QcWorldBackup.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QTest>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

using namespace Qt::StringLiterals;

namespace {

// Holds the background copy until the test opens it.
class Gate
{
public:
    void wait()
    {
        std::unique_lock lock(m_mutex);
        m_entered = true;
        m_changed.notify_all();
        m_changed.wait(lock, [this] { return m_open; });
    }

    void open()
    {
        const std::scoped_lock lock(m_mutex);
        m_open = true;
        m_changed.notify_all();
    }

    bool entered()
    {
        const std::scoped_lock lock(m_mutex);
        return m_entered;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_changed;
    bool m_entered = false;
    bool m_open = false;
};

QByteArray contents(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void replace(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
    QVERIFY(file.commit());
}

} // namespace

// /backup in the background (decision E-256): the snapshot freezes the saved files at once, the
// ticks go on during the copy, files written meanwhile do not reach the backup, and a stopping
// server waits for the copy.
class TestWorldBackup : public QcServerTestFixture
{
    Q_OBJECT

private:
    QString lastLine() const { return m_client->chatLines.isEmpty() ? QString() : m_client->chatLines.constLast(); }

    QStringList backupsIn(const QString &root) const { return QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot); }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void aSnapshotKeepsTheSavedFiles()
    {
        QTemporaryDir data;
        const QString world = QDir(data.path()).filePath(u"worlds/mine"_s);
        QVERIFY(QDir().mkpath(world + u"/regions"_s));
        replace(QDir(world).filePath(u"world.json"_s), "old world");
        replace(QDir(world).filePath(u"regions/r.0.0.qcr"_s), "old region");
        const QString root = QDir(data.path()).filePath(u"backups"_s);

        const QcResult<QcWorldBackup::Snapshot> snapshot = QcWorldBackup::snapshot(world, root, u"x"_s, QDateTime::currentDateTime());
        QVERIFY(snapshot.has_value());
        QVERIFY(snapshot->linked);
        QCOMPARE(snapshot->files.size(), 2);
        // Hidden while it is not done.
        QVERIFY(QFileInfo(snapshot->staging).fileName().startsWith(u'.'));
        QCOMPARE(backupsIn(root), QStringList());

        // The game replaces its files (never writes into them): the snapshot keeps the old ones.
        replace(QDir(world).filePath(u"regions/r.0.0.qcr"_s), "new region");
        replace(QDir(world).filePath(u"world.json"_s), "new world");
        const QcResult<QcWorldBackup::Summary> done = QcWorldBackup::complete(*snapshot);
        QVERIFY(done.has_value());
        QCOMPARE(done->files, 2);
        QCOMPARE(contents(QDir(done->directory).filePath(u"regions/r.0.0.qcr"_s)), QByteArray("old region"));
        QCOMPARE(contents(QDir(done->directory).filePath(u"world.json"_s)), QByteArray("old world"));
        QCOMPARE(contents(QDir(world).filePath(u"world.json"_s)), QByteArray("new world"));
        QCOMPARE(QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden), QStringList{QFileInfo(done->directory).fileName()});

        // The copy is a copy: changing it leaves the world alone.
        replace(QDir(done->directory).filePath(u"world.json"_s), "changed backup");
        QCOMPARE(contents(QDir(world).filePath(u"world.json"_s)), QByteArray("new world"));
    }

    void theTicksGoOnWhileTheBackupIsCopied()
    {
        QTemporaryDir data;
        QcServerConfig config;
        config.worldDirectory = QDir(data.path()).filePath(u"worlds/mine"_s);
        QVERIFY(QDir().mkpath(config.worldDirectory));
        join(config);
        const QcBlockPos pos = m_feet.offset(4, 0, 0);
        chat(u"/setblock ~4 ~ ~ glass"_s);

        // Opened at the end in any case, so a failing check does not leave the copy waiting.
        const auto gate = std::make_shared<Gate>();
        const auto openAtEnd = qScopeGuard([gate] { gate->open(); });
        m_server->commands().backups().setBeforeCopy([gate] { gate->wait(); });
        chat(u"/backup"_s);
        QVERIFY2(lastLine().startsWith(u"Backup of "_s), qPrintable(lastLine()));
        QTRY_VERIFY(gate->entered());

        // The copy waits; the server ticks, plays and saves meanwhile.
        const quint64 before = m_server->tickCount();
        ticks(20);
        QCOMPARE(m_server->tickCount(), before + 20);
        chat(u"/setblock ~4 ~ ~ stone"_s);
        chat(u"/save"_s);
        QCOMPARE(lastLine(), u"World saved"_s);
        chat(u"/backup"_s);
        QCOMPARE(lastLine(), u"A backup is still being copied, try again when it is done"_s);

        gate->open();
        for (int i = 0; i < 400 && !lastLine().startsWith(u"Backup with "_s); ++i) {
            QTest::qWait(5);
            ticks(1);
        }
        QVERIFY2(lastLine().startsWith(u"Backup with "_s), qPrintable(lastLine()));

        // The backup holds the world of the moment of /backup: glass, not the later stone.
        const QString root = QDir(data.path()).filePath(u"backups"_s);
        QCOMPARE(backupsIn(root).size(), 1);
        cleanup();
        config.worldDirectory = QDir(root).filePath(backupsIn(root).constFirst());
        join(config);
        QCOMPARE(*m_server->world().blockAt(pos), id("qtcraft:glass"));
    }

    void aStoppingServerWaitsForTheCopy()
    {
        QTemporaryDir data;
        QcServerConfig config;
        config.worldDirectory = QDir(data.path()).filePath(u"worlds/mine"_s);
        QVERIFY(QDir().mkpath(config.worldDirectory));
        join(config);

        const auto gate = std::make_shared<Gate>();
        const auto openAtEnd = qScopeGuard([gate] { gate->open(); });
        m_server->commands().backups().setBeforeCopy([gate] { gate->wait(); });
        chat(u"/backup"_s);
        QTRY_VERIFY(gate->entered());
        std::thread opener([gate] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            gate->open();
        });
        QVERIFY(m_server->stop().has_value());
        opener.join();
        QCOMPARE(backupsIn(QDir(data.path()).filePath(u"backups"_s)).size(), 1);
        QCOMPARE(QDir(QDir(data.path()).filePath(u"backups"_s)).entryList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestWorldBackup)
#include "test_world_backup.moc"

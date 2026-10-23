#include "common/content/loader/QcBaseContent.h"
#include "network/QcTcpConnector.h"
#include "QcRawClient.h"
#include "server/network/QcServerSettings.h"
#include "server/storage/QcWorldStorage.h"
#include "worlds/QcContentFingerprint.h"
#include "worlds/QcModLoader.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <csignal>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 30'000;

void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

bool copyDirectory(const QString &from, const QString &to)
{
    QDirIterator it(from, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString source = it.next();
        const QString target = QDir(to).filePath(QDir(from).relativeFilePath(source));
        if (!QDir().mkpath(QFileInfo(target).path()) || !QFile::copy(source, target)) {
            return false;
        }
    }
    return true;
}

} // namespace

// The dedicated server qtcraft-headless --server (decision E-255): server.json and the command
// line, a process that saves on SIGTERM and SIGINT, runs without standard input and lets clients
// in over TCP; and the content fingerprints compared at the login (decision E-252).
class TestDedicatedServer : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_data;
    QProcess m_process;
    QString m_output;

    // Starts the server and waits for its port; 0 when it did not come up.
    quint16 startServer(const QStringList &extra, const QProcessEnvironment &environment = QProcessEnvironment::systemEnvironment())
    {
        m_output.clear();
        m_process.setProgram(QStringLiteral(QTCRAFT_HEADLESS_PATH));
        m_process.setArguments(QStringList{u"--server"_s, u"--data-dir"_s, m_data.path(), u"--bind"_s, u"127.0.0.1"_s, u"--port"_s, u"0"_s,
                                           u"--no-mods"_s, u"--time-limit"_s, u"90"_s}
                               + extra);
        m_process.setProcessEnvironment(environment);
        m_process.setProcessChannelMode(QProcess::SeparateChannels);
        m_process.start();
        if (!m_process.waitForStarted(timeoutMs)) {
            return 0;
        }
        // A service: nothing on standard input.
        m_process.closeWriteChannel();

        static const QRegularExpression listening(u"Listening on port (\\d+)"_s);
        quint16 port = 0;
        [[maybe_unused]] const bool done = QTest::qWaitFor([&] {
            m_output += QString::fromUtf8(m_process.readAllStandardOutput());
            const QRegularExpressionMatch match = listening.match(m_output);
            port = match.hasMatch() ? static_cast<quint16>(match.captured(1).toUInt()) : 0;
            return port != 0 || m_process.state() == QProcess::NotRunning;
        }, timeoutMs);
        return port;
    }

    std::unique_ptr<QcRawClient> join(quint16 port, const QString &name, const QString &password = {})
    {
        QcTcpConnector connector;
        QSignalSpy connected(&connector, &QcTcpConnector::connected);
        connector.connectTo({u"127.0.0.1"_s, port});
        if (!connected.wait(timeoutMs)) {
            return nullptr;
        }
        auto client = std::make_unique<QcRawClient>(connector.takeTransport());
        client->password = password;
        client->hello(name);
        [[maybe_unused]] const bool answered = QTest::qWaitFor([&] { return client->entityId != 0 || !client->disconnectReason.isEmpty(); }, timeoutMs);
        return client;
    }

    QString finish()
    {
        m_process.waitForFinished(timeoutMs);
        m_output += QString::fromUtf8(m_process.readAllStandardOutput());
        return m_output;
    }

    QString worldDirectory(const QString &folder = u"World"_s) const { return QDir(m_data.path()).filePath(u"worlds/"_s + folder); }

private slots:
    void cleanup()
    {
        if (m_process.state() != QProcess::NotRunning) {
            m_process.kill();
            m_process.waitForFinished(timeoutMs);
        }
        QDir(m_data.path()).removeRecursively();
        QDir().mkpath(m_data.path());
    }

    void settingsReadWriteAndRefuseNonsense()
    {
        QcServerSettings defaults;
        QCOMPARE(defaults.port, quint16(25600));
        QCOMPARE(QcServerSettings::read(m_data.filePath(u"missing.json"_s))->maxPlayers, 8);

        QcServerSettings settings;
        settings.serverName = u"Kiste"_s;
        settings.port = 4000;
        settings.maxPlayers = 3;
        settings.password = u"pw"_s;
        settings.world = u"Island"_s;
        settings.seed = -42;
        settings.viewDistance = 6;
        settings.bindAddress = u"::1"_s;
        settings.operators = {u"Ann"_s};
        const QString path = m_data.filePath(u"server.json"_s);
        QVERIFY(settings.write(path).has_value());
        QCOMPARE(QFile(path).permissions() & QFileDevice::ReadOther, QFileDevice::Permissions());
        const QcResult<QcServerSettings> read = QcServerSettings::read(path);
        QVERIFY(read.has_value());
        QCOMPARE(read->toJson(), settings.toJson());
        QCOMPARE(*read->seed, -42);

        const QcServerConfig config = read->serverConfig();
        QCOMPARE(config.serverName, u"Kiste"_s);
        QCOMPARE(config.maxPlayers, 3);
        QCOMPARE(config.password, u"pw"_s);
        QCOMPARE(config.viewDistance, 6);
        QCOMPARE(config.operators, QStringList{u"Ann"_s});
        const QcNetworkConfig network = read->networkConfig();
        QCOMPARE(network.port, quint16(4000));
        QCOMPARE(network.bindAddress, QHostAddress(QHostAddress::LocalHostIPv6));

        // An empty seed is a random one.
        QVERIFY(!QcServerSettings::fromJson(QJsonObject{{u"seed"_s, u""_s}})->seed);
        for (const QJsonObject &bad : {QJsonObject{{u"port"_s, 70000}}, QJsonObject{{u"maxPlayers"_s, 0}}, QJsonObject{{u"viewDistance"_s, 99}},
                                       QJsonObject{{u"port"_s, u"25600"_s}}, QJsonObject{{u"bindAddress"_s, u"nowhere"_s}},
                                       QJsonObject{{u"seed"_s, u"abc"_s}}, QJsonObject{{u"motd"_s, QString(300, u'x')}},
                                       QJsonObject{{u"maxPlayers"_s, 2.5}}, QJsonObject{{u"operators"_s, u"Ann"_s}},
                                       QJsonObject{{u"operators"_s, QJsonArray{u"..."_s}}}, QJsonObject{{u"operators"_s, QJsonArray{u"<b>Ann</b>"_s}}},
                                       QJsonObject{{u"operators"_s, QJsonArray{u" Ann "_s}}}, QJsonObject{{u"operators"_s, QJsonArray{5}}}}) {
            QVERIFY2(!QcServerSettings::fromJson(bad).has_value(), QJsonDocument(bad).toJson().constData());
        }
        // Invalid operator names are named instead of becoming "Player".
        const QcResult<QcServerSettings> badOperator = QcServerSettings::fromJson(QJsonObject{{u"operators"_s, QJsonArray{u"Ann"_s, u"?!"_s}}});
        QVERIFY(!badOperator && badOperator.error().contains(u"\"?!\""_s));
        writeFile(path, "{ no json");
        QVERIFY(!QcServerSettings::read(path).has_value());
    }

    void theCommandLineOverridesTheFile()
    {
        QcServerSettings file;
        file.port = 1;
        file.maxPlayers = 3;
        file.motd = u"from the file"_s;
        file.world = u"Island"_s;
        file.seed = 7;
        QVERIFY(file.write(m_data.filePath(u"server.json"_s)).has_value());

        const quint16 port = startServer({u"--max-players"_s, u"5"_s, u"--time-limit"_s, u"2"_s});
        QVERIFY2(port != 0 && port != 1, qPrintable(m_output));
        const QString output = finish();
        QVERIFY2(output.contains(u"up to 5 players"_s), qPrintable(output));
        QVERIFY(output.contains(u"Created the world \"Island\" (seed 7)"_s));
        QVERIFY(QFile::exists(QDir(worldDirectory(u"Island"_s)).filePath(u"world.json"_s)));
        // The time limit ends it with code 2, after saving.
        QCOMPARE(m_process.exitCode(), 2);
        QVERIFY(output.contains(u"World saved"_s));
    }

    void aMissingSettingsFileIsWritten()
    {
        const quint16 port = startServer({u"--time-limit"_s, u"1"_s});
        QVERIFY(port != 0);
        finish();
        const QcResult<QcServerSettings> written = QcServerSettings::read(m_data.filePath(u"server.json"_s));
        QVERIFY(written.has_value());
        // The file has the defaults, not the command line.
        QCOMPARE(written->port, quint16(25600));
        QCOMPARE(written->bindAddress, QString());
    }

    void invalidSettingsStopTheStart()
    {
        writeFile(m_data.filePath(u"server.json"_s), R"({"maxPlayers": -1})");
        QCOMPARE(startServer({}), quint16(0));
        finish();
        QCOMPARE(m_process.exitCode(), 1);
        QVERIFY(m_process.readAllStandardError().contains("maxPlayers"));
    }

    void invalidNumbersOnTheCommandLineStopTheStart()
    {
        // A valid seed after an invalid number must not hide it.
        for (const QStringList &options : {QStringList{u"--max-players"_s, u"0"_s, u"--seed"_s, u"5"_s},
                                           QStringList{u"--port"_s, u"70000"_s, u"--seed"_s, u"1"_s}}) {
            QCOMPARE(startServer(options), quint16(0));
            finish();
            QCOMPARE(m_process.exitCode(), 1);
            QVERIFY2(m_process.readAllStandardError().contains("Invalid number"), qPrintable(options.join(u' ')));
        }
    }

    void sigtermSavesThePlayersAndTheWorld()
    {
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(u"QTCRAFT_SERVER_PASSWORD"_s, u"letmein"_s);
        const quint16 port = startServer({}, environment);
        QVERIFY2(port != 0, qPrintable(m_output));

        QCOMPARE(join(port, u"Nopass"_s)->disconnectKey, u"Wrong server password"_s);
        std::unique_ptr<QcRawClient> ann = join(port, u"Ann"_s, u"letmein"_s);
        QVERIFY(ann && ann->entityId != 0);
        QVERIFY(ann->challenge->serverName == u"QtCraft server"_s);
        const QString playerFile = QDir(worldDirectory()).filePath(u"players/"_s + QcWorldStorage::playerFileName(u"Ann"_s));
        QVERIFY(!QFile::exists(playerFile));

        // The process runs on without standard input until SIGTERM, which saves everything.
        QTest::qWait(300);
        QCOMPARE(m_process.state(), QProcess::Running);
        m_process.terminate();
        const QString output = finish();
        QCOMPARE(m_process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(m_process.exitCode(), 0);
        QVERIFY2(output.contains(u"Stopping (SIGTERM)"_s) && output.contains(u"World saved"_s), qPrintable(output));
        QVERIFY(QFile::exists(playerFile));
        QTRY_COMPARE_WITH_TIMEOUT(ann->disconnectKey, u"The server stopped"_s, timeoutMs);
        // The world is closed properly: no lock is left.
        QVERIFY(!QFile::exists(QDir(worldDirectory()).filePath(u"session.lock"_s)));
    }

    void sigintSavesToo()
    {
        const quint16 port = startServer({});
        QVERIFY(port != 0);
        ::kill(static_cast<pid_t>(m_process.processId()), SIGINT);
        const QString output = finish();
        QCOMPARE(m_process.exitCode(), 0);
        QVERIFY2(output.contains(u"Stopping (SIGINT)"_s) && output.contains(u"World saved"_s), qPrintable(output));
    }

    void contentFingerprintsTellPacksApart()
    {
        // The base game: hashed from its resources, the same every time.
        const QcModLoader::Loaded base = QcModLoader::load(QString());
        QVERIFY(base.has_value());
        const QList<QcPackFingerprint> basePacks = QcContentFingerprint::of(*base->content, base->mods);
        QCOMPARE(basePacks.size(), 1);
        QCOMPARE(basePacks.first().id, u"qtcraft"_s);
        QCOMPARE(basePacks.first().hash.size(), 32);
        QCOMPARE(QcContentFingerprint::of(*base->content, base->mods), basePacks);

        // With the example mod: a second pack with its own hash.
        const QString mods = m_data.filePath(u"mods"_s);
        QVERIFY(copyDirectory(QStringLiteral(QTCRAFT_MODS_SOURCE_DIR "/rubies"), QDir(mods).filePath(u"rubies"_s)));
        const QcModLoader::Loaded withMod = QcModLoader::load(mods);
        QVERIFY(withMod.has_value());
        const QList<QcPackFingerprint> modPacks = QcContentFingerprint::of(*withMod->content, withMod->mods);
        QCOMPARE(modPacks.size(), 2);
        QCOMPARE(modPacks.first(), basePacks.first());
        QCOMPARE(modPacks[1].id, withMod->content->packs()[1].id);
        QCOMPARE(modPacks[1].hash.size(), 32);

        // Files the loader does not read do not count: hidden files (.DS_Store), notes, subfolders
        // of content folders.
        const QString directory = QDir(mods).filePath(u"rubies"_s);
        const QString before = *QcContentFingerprint::hashOf(directory);
        QCOMPARE(*QcContentFingerprint::hashOf(directory), before);
        writeFile(QDir(directory).filePath(u".DS_Store"_s), "finder");
        writeFile(QDir(directory).filePath(u"items/._ruby.json"_s), "apple double");
        writeFile(QDir(directory).filePath(u"README.txt"_s), "notes");
        writeFile(QDir(directory).filePath(u"items/old/ruby.json"_s), "{}");
        QCOMPARE(*QcContentFingerprint::hashOf(directory), before);

        // Any change of a file it reads, its name or a new one changes the hash.
        writeFile(QDir(directory).filePath(u"items/extra.json"_s), "{}");
        const QString added = *QcContentFingerprint::hashOf(directory);
        QVERIFY(added != before);
        QVERIFY(QFile::rename(QDir(directory).filePath(u"items/extra.json"_s), QDir(directory).filePath(u"items/other.json"_s)));
        QVERIFY(*QcContentFingerprint::hashOf(directory) != added);
        writeFile(QDir(directory).filePath(u"items/other.json"_s), "{ }");
        QVERIFY(*QcContentFingerprint::hashOf(directory) != added);
        writeFile(QDir(directory).filePath(u"pack.json"_s), "{}");
        QVERIFY(*QcContentFingerprint::hashOf(directory) != added);

        // The village files of decision E-421 count: a profession and a villager species.
        writeFile(QDir(directory).filePath(u"professions/baker.json"_s), R"({"name": "baker"})");
        const QString withProfession = *QcContentFingerprint::hashOf(directory);
        writeFile(QDir(directory).filePath(u"professions/baker.json"_s), R"({"name": "baker", "offersPerLevel": 3})");
        QVERIFY(*QcContentFingerprint::hashOf(directory) != withProfession);
        const QString changedProfession = *QcContentFingerprint::hashOf(directory);
        writeFile(QDir(directory).filePath(u"villagers/gnome.json"_s), R"({"name": "gnome"})");
        QVERIFY(*QcContentFingerprint::hashOf(directory) != changedProfession);
    }
};

QTEST_GUILESS_MAIN(TestDedicatedServer)
#include "test_dedicated_server.moc"

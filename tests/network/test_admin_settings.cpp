#include "common/diagnostics/QcMessageLog.h"
#include "network/QcAdminTestFixture.h"
#include "server/admin/QcAdminReports.h"
#include "server/admin/QcAdminStatusRecorder.h"
#include "server/commands/QcGameRules.h"
#include "server/core/QcServerLogging.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcSettingsTest, "qtcraft.settingstest")

const QcMsg::AdminSettings::Setting *setting(const QList<QcMsg::AdminSettings::Setting> &list, const QString &key)
{
    const auto found = std::ranges::find(list, key, &QcMsg::AdminSettings::Setting::key);
    return found == list.end() ? nullptr : &*found;
}

} // namespace

// Game rules and server limits through the admin channel and where they are kept, the log's tail,
// saving, and the status history's arithmetic (decision E-848).
class TestAdminSettings : public QcAdminTestFixture
{
    Q_OBJECT

private slots:
    void initTestCase() { QcMessageLog::installWithoutFile(); }
    void cleanupTestCase() { QcMessageLog::uninstall(); }

    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void settingsDescribeEveryRuleAndLimit()
    {
        join();
        QVERIFY(ask(Kind::Settings)->ok);
        const QcMsg::AdminSettings settings = *latest<QcMsg::AdminSettings>();
        QCOMPARE(settings.rules.size(), qsizetype(QcGameRules::all().size()));
        QCOMPARE(settings.limits.size(), qsizetype(QcServerLimits::fields().size()));
        QVERIFY(!settings.limitsFromServer);

        // Defaults of the tables are the values of a fresh world and fresh limits.
        const QcWorldInfo freshWorld;
        for (const QcGameRules::Rule &rule : QcGameRules::all()) {
            QCOMPARE(qint64(rule.defaultValue), QcGameRules::value(rule, freshWorld));
            QVERIFY(rule.group && rule.description);
        }
        for (const QcMsg::AdminSettings::Setting &limit : settings.limits) {
            QVERIFY2(!limit.group.isEmpty() && !limit.description.isEmpty(), qPrintable(limit.key));
            QVERIFY(limit.low <= limit.defaultValue && limit.defaultValue <= limit.high);
            QCOMPARE(limit.value, limit.defaultValue);
        }
        const QcMsg::AdminSettings::Setting *keep = setting(settings.rules, u"keepInventory"_s);
        QVERIFY(keep);
        QCOMPARE(keep->type, QcMsg::AdminSettings::Type::Boolean);
        QCOMPARE(keep->value, 1);
    }

    void rulesChangeLikeGamerule()
    {
        join();
        const std::optional<QcMsg::AdminResult> result = ask(Kind::SetRule, {u"keepInventory"_s, u"false"_s});
        QVERIFY(result && result->ok);
        QVERIFY(!m_server->keepInventory());
        QCOMPARE(setting(latest<QcMsg::AdminSettings>()->rules, u"keepInventory"_s)->value, 0);

        QVERIFY(ask(Kind::SetRule, {u"maxEntityCramming"_s, u"8"_s})->ok);
        QCOMPARE(setting(latest<QcMsg::AdminSettings>()->rules, u"maxEntityCramming"_s)->value, 8);
        // Out of range or unknown: the command's usage, nothing changes.
        const std::optional<QcMsg::AdminResult> wrong = ask(Kind::SetRule, {u"maxEntityCramming"_s, u"999999"_s});
        QVERIFY(texts(*wrong).join(u' ').contains(u"Usage: /gamerule"_s));
        QCOMPARE(setting(latest<QcMsg::AdminSettings>()->rules, u"maxEntityCramming"_s)->value, 8);
    }

    void limitsChangeWithinTheirRange()
    {
        join();
        QVERIFY(ask(Kind::SetLimit, {u"summonLimit"_s, u"5"_s})->ok);
        QCOMPARE(m_server->config().limits.summonLimit, 5);
        QCOMPARE(setting(latest<QcMsg::AdminSettings>()->limits, u"summonLimit"_s)->value, 5);

        const std::optional<QcMsg::AdminResult> outside = ask(Kind::SetLimit, {u"summonLimit"_s, u"100000"_s});
        QVERIFY(texts(*outside).join(u' ').contains(u"from 0 to 4096"_s));
        QCOMPARE(m_server->config().limits.summonLimit, 5);
        QVERIFY(texts(*ask(Kind::SetLimit, {u"noSuchLimit"_s, u"1"_s})).join(u' ').contains(u"Unknown limit"_s));

        QVERIFY(ask(Kind::SetLimit, {u"summonLimit"_s, u"default"_s})->ok);
        QCOMPARE(m_server->config().limits.summonLimit, QcServerLimits{}.summonLimit);

        // The console's command does the same.
        console(u"limits set crowdIdle 30"_s);
        QCOMPARE(m_server->config().limits.crowdIdle, 30);
    }

    void worldSettingsAreSavedInTheWorld()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.filePath(u"world"_s);
        join(config);
        QVERIFY(ask(Kind::SetRule, {u"mobGriefing"_s, u"false"_s})->ok);
        QVERIFY(ask(Kind::SetLimit, {u"fires"_s, u"100"_s})->ok);
        QVERIFY(texts(*ask(Kind::Save)).join(u' ').contains(u"World saved"_s));
        m_server->waitForStorage();
        cleanup();

        join(config);
        QVERIFY(ask(Kind::Settings)->ok);
        const QcMsg::AdminSettings settings = *latest<QcMsg::AdminSettings>();
        QCOMPARE(setting(settings.rules, u"mobGriefing"_s)->value, 0);
        QCOMPARE(setting(settings.limits, u"fires"_s)->value, 100);
        QCOMPARE(m_server->config().limits.fires, 100);
    }

    void serverLimitsAreWrittenToServerJson()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"server.json"_s);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"serverName": "Kept", "limits": {"fires": 200}})");
        file.close();

        QcServerConfig config;
        config.limitsFromServer = true;
        config.settingsFile = path;
        join(config);
        QVERIFY(ask(Kind::SetLimit, {u"summonLimit"_s, u"9"_s})->ok);
        QVERIFY(texts(*latest<QcMsg::AdminResult>()).join(u' ').contains(u"server.json"_s));
        QVERIFY(ask(Kind::SetLimit, {u"fires"_s, u"default"_s})->ok);

        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(json.value(u"serverName"_s).toString(), u"Kept"_s);
        QCOMPARE(json.value(u"limits"_s).toObject().value(u"summonLimit"_s).toInt(), 9);
        QVERIFY(!json.value(u"limits"_s).toObject().contains(u"fires"_s));
        QVERIFY(latest<QcMsg::AdminSettings>()->limitsFromServer);
    }

    void theLogTailIsBounded()
    {
        // A dedicated server shows its whole log.
        QcServerConfig config;
        config.fullAdminLog = true;
        join(config);
        for (int i = 0; i < 30; ++i) {
            qCInfo(lcSettingsTest).noquote() << u"info %1"_s.arg(i);
        }
        QTest::ignoreMessage(QtWarningMsg, "careful");
        qCWarning(lcSettingsTest) << "careful";
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Log tail of Raw: level 2 and 5000 lines bounded to 2 and 2000"_s));
        QVERIFY(ask(Kind::LogTail, {u"2"_s, u"5000"_s})->ok);
        const QcMsg::AdminLog warnings = *latest<QcMsg::AdminLog>();
        QVERIFY(warnings.tail);
        QVERIFY(!warnings.lines.isEmpty());
        QVERIFY(warnings.lines.size() <= QcMsg::AdminLimits::maxLogLines);
        QVERIFY(std::ranges::all_of(warnings.lines, [](const auto &line) { return line.level >= QcMsg::AdminLogLine::Level::Warning; }));
        // The warning about the bounds comes after it.
        QVERIFY(std::ranges::any_of(warnings.lines, [](const auto &line) { return line.text == u"careful"_s; }));

        // At most one tail a second.
        const std::optional<QcMsg::AdminResult> tooSoon = ask(Kind::LogTail, {u"1"_s, u"10"_s});
        QVERIFY(tooSoon && !tooSoon->ok);
        QCOMPARE(tooSoon->refusal, QcMsg::AdminResult::Refusal::TooSoon);
        ticks(20);
        QVERIFY(ask(Kind::LogTail, {u"1"_s, u"10"_s})->ok);
        QCOMPARE(latest<QcMsg::AdminLog>()->lines.size(), 10);

        // Following the log brings the tail, then the new lines once a second.
        follow(QcMsg::AdminSubscribe::log, 1);
        qCInfo(lcSettingsTest) << "after following";
        ticks(25);
        QCOMPARE(latest<QcMsg::AdminLog>()->tail, false);
        QVERIFY(std::ranges::any_of(latest<QcMsg::AdminLog>()->lines, [](const auto &line) { return line.text == u"after following"_s; }));
    }

    void followedLogLinesComeOnce()
    {
        QcServerConfig config;
        config.fullAdminLog = true;
        join(config);
        follow(QcMsg::AdminSubscribe::log, 1);
        for (int tick = 0; tick < 120; ++tick) {
            qCInfo(lcSettingsTest).noquote() << u"once %1"_s.arg(tick);
            ticks(1);
        }
        // The lines of the last second come with the next one.
        ticks(20);
        QStringList seen;
        const QList<QcMsg::AdminLog> logs = received<QcMsg::AdminLog>();
        for (const QcMsg::AdminLog &log : logs) {
            for (const QcMsg::AdminLogLine &line : log.lines) {
                if (line.text.startsWith(u"once "_s)) {
                    seen.append(line.text);
                }
            }
        }
        QCOMPARE(seen.size(), 120);
        QCOMPARE(QSet<QString>(seen.begin(), seen.end()).size(), 120);
    }

    void aWorldInTheGameShowsOnlyTheServersLog()
    {
        // The default: a server in the game's process shares its log with the host's game.
        join();
        qCInfo(lcSettingsTest) << "the host's own line";
        qCInfo(lcServer) << "a line of the server";
        ticks(20);
        QVERIFY(ask(Kind::LogTail, {u"0"_s, u"2000"_s})->ok);
        const QcMsg::AdminLog tail = *latest<QcMsg::AdminLog>();
        QVERIFY(std::ranges::any_of(tail.lines, [](const auto &line) { return line.text == u"a line of the server"_s; }));
        QVERIFY(std::ranges::none_of(tail.lines, [](const auto &line) { return line.category == u"qtcraft.settingstest"_s; }));
        QVERIFY(std::ranges::all_of(tail.lines, [](const auto &line) { return QcAdminReports::serverCategories().contains(line.category); }));
    }

    void backupWithoutAWorldSaysSo()
    {
        join();
        QVERIFY(texts(*ask(Kind::Backup)).join(u' ').contains(u"not saved"_s));
    }

    void theRecorderFoldsSecondsIntoMinutes()
    {
        QcAdminStatusRecorder recorder;
        for (int second = 0; second < 125; ++second) {
            for (int tick = 0; tick < 20; ++tick) {
                recorder.noteTick(tick == 19 ? 40.0 : 2.0);
            }
            recorder.sample({qint64(second) * 1000, 2, 100, 10, 300.0f, quint64(second) * 2048, 0}, 1.0);
        }
        QCOMPARE(recorder.seconds().size(), 125);
        QCOMPARE(recorder.minutes().size(), 2);
        const QcMsg::AdminSample second = recorder.seconds().last();
        QCOMPARE(second.ticksPerSecond, 20.0f);
        QCOMPARE(second.tickMaxMs, 40.0f);
        // 19 of 20 ticks took 2 ms: the 99th percentile is the slow one (nearest rank 20).
        QCOMPARE(second.tickP99Ms, 40.0f);
        QCOMPARE(second.tickMeanMs, 3.9f);
        QCOMPARE(second.sentKbPerSecond, 2.0f);
        QCOMPARE(recorder.minutes().first().tickMaxMs, 40.0f);
        QCOMPARE(recorder.minutes().first().timeMs, 59'000);

        for (int second = 0; second < QcAdminStatusRecorder::secondsKept; ++second) {
            recorder.sample({}, 1.0);
        }
        QCOMPARE(recorder.seconds().size(), QcAdminStatusRecorder::secondsKept);
    }
};

QTEST_GUILESS_MAIN(TestAdminSettings)
#include "test_admin_settings.moc"

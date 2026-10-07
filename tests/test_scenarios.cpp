#include "QcBaseContent.h"
#include "QcClientSession.h"
#include "QcControlInterface.h"
#include "QcLocalServerHost.h"
#include "QcTestTerrainGenerator.h"
#include "QcTransport.h"

#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 30000;

} // namespace

// End-to-end scenarios through the control interface: server thread, protocol, client session.
class TestScenarios : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QcLocalServerHost> m_host;
    std::unique_ptr<QcClientSession> m_session;
    std::unique_ptr<QBuffer> m_output;
    std::unique_ptr<QcControlInterface> m_control;
    qint64 m_readOffset = 0;

    QJsonObject request(const QJsonObject &command)
    {
        m_control->submit(command);
        QJsonObject response;
        const bool answered = QTest::qWaitFor(
            [&] {
                const QByteArray &data = m_output->data();
                const qsizetype newline = data.indexOf('\n', m_readOffset);
                if (newline < 0) {
                    return false;
                }
                response = QJsonDocument::fromJson(data.mid(m_readOffset, newline - m_readOffset)).object();
                m_readOffset = newline + 1;
                return true;
            },
            timeoutMs);
        if (!answered) {
            qWarning("no response to %s", QJsonDocument(command).toJson(QJsonDocument::Compact).constData());
        }
        return response;
    }

    static QcVec3 position(const QJsonObject &status)
    {
        const QJsonArray p = status.value(u"position"_s).toArray();
        return {p.at(0).toDouble(), p.at(1).toDouble(), p.at(2).toDouble()};
    }

private slots:
    void init()
    {
        QStringList warnings;
        std::shared_ptr<const QcContentRegistry> content = *QcBaseContent::load(warnings);
        QcServerConfig config;
        config.viewDistance = 2;
        config.worldName = u"Scenario"_s;
        m_host = std::make_unique<QcLocalServerHost>(config, content, std::make_unique<QcTestTerrainGenerator>(content));
        m_session = std::make_unique<QcClientSession>(m_host->connectClient(), u"Bot"_s);
        m_output = std::make_unique<QBuffer>();
        m_output->open(QIODevice::ReadWrite);
        m_control = std::make_unique<QcControlInterface>(m_session.get(), nullptr, m_output.get());
        m_readOffset = 0;
        m_session->start();
        const QJsonObject ready = request({{u"cmd"_s, u"waitUntilReady"_s}});
        QVERIFY2(ready.value(u"ok"_s).toBool(), QJsonDocument(ready).toJson().constData());
    }

    void cleanup()
    {
        m_control.reset();
        m_session.reset();
        m_host->shutdown();
        m_host.reset();
    }

    void statusDescribesThePlayer()
    {
        const QJsonObject status = request({{u"cmd"_s, u"status"_s}, {u"id"_s, 7}});
        QCOMPARE(status.value(u"id"_s).toInt(), 7);
        QCOMPARE(status.value(u"state"_s).toString(), u"Playing"_s);
        QCOMPARE(status.value(u"world"_s).toString(), u"Scenario"_s);
        QVERIFY(status.value(u"onGround"_s).toBool());
        QCOMPARE(status.value(u"loadedChunks"_s).toInt(), 25);
    }

    void walkingSouthIncreasesZ()
    {
        const QcVec3 start = position(request({{u"cmd"_s, u"look"_s}, {u"yaw"_s, 180}}));
        const QJsonObject after = request({{u"cmd"_s, u"move"_s}, {u"forward"_s, 1}, {u"ticks"_s, 20}});
        QVERIFY(after.value(u"ok"_s).toBool());
        const QcVec3 end = position(after);
        QVERIFY2(end.z > start.z + 0.5, qPrintable(QString::number(end.z - start.z)));
        QVERIFY(std::abs(end.x - start.x) < 1e-9);
    }

    void jumpLeavesTheGroundAndLandsAgain()
    {
        const QJsonObject inAir = request({{u"cmd"_s, u"jump"_s}});
        QVERIFY(!inAir.value(u"onGround"_s).toBool());
        QVERIFY(position(inAir).y > 0.0);
        const QJsonObject landed = request({{u"cmd"_s, u"wait"_s}, {u"ticks"_s, 30}});
        QVERIFY(landed.value(u"onGround"_s).toBool());
    }

    void blockQueriesSeeSolidGroundBelowThePlayer()
    {
        const QcVec3 feet = position(request({{u"cmd"_s, u"status"_s}}));
        const QJsonObject below = request({{u"cmd"_s, u"block"_s},
                                           {u"x"_s, int(std::floor(feet.x))},
                                           {u"y"_s, int(std::floor(feet.y)) - 1},
                                           {u"z"_s, int(std::floor(feet.z))}});
        const QString name = below.value(u"block"_s).toString();
        QVERIFY2(name == u"qtcraft:grass_block"_s || name == u"qtcraft:sand"_s, qPrintable(name));
        const QJsonObject around = request({{u"cmd"_s, u"blocks"_s}, {u"radius"_s, 1}});
        QVERIFY(!around.value(u"blocks"_s).toArray().isEmpty());
    }

    void errorsAreReportedNotFatal()
    {
        const QJsonObject unknown = request({{u"cmd"_s, u"fly"_s}, {u"id"_s, u"x"_s}});
        QCOMPARE(unknown.value(u"ok"_s).toBool(true), false);
        QCOMPARE(unknown.value(u"id"_s).toString(), u"x"_s);
        QCOMPARE(m_control->failedRequests(), 1);
        QVERIFY(request({{u"cmd"_s, u"status"_s}}).value(u"ok"_s).toBool());
    }

    void malformedLinesFromTheInputDeviceAreRejected()
    {
        QBuffer input;
        input.open(QIODevice::ReadWrite);
        QBuffer output;
        output.open(QIODevice::ReadWrite);
        QcControlInterface control(m_session.get(), &input, &output);
        input.write("not json\n{\"cmd\":\"status\"}\n");
        input.seek(0);
        emit input.readyRead();
        QTRY_VERIFY_WITH_TIMEOUT(output.data().count('\n') >= 2, timeoutMs);
        const QList<QByteArray> lines = output.data().split('\n');
        QCOMPARE(QJsonDocument::fromJson(lines.at(0)).object().value(u"ok"_s).toBool(true), false);
        QCOMPARE(QJsonDocument::fromJson(lines.at(1)).object().value(u"ok"_s).toBool(), true);
    }
};

QTEST_GUILESS_MAIN(TestScenarios)
#include "test_scenarios.moc"

// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/control/QcControlInterface.h"
#include "clientcore/player/QcClientAdvancements.h"
#include "clientcore/session/QcClientSession.h"
#include "QcTestUtil.h"
#include "server/core/QcServerConfig.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"
#include "worlds/QcLocalServerHost.h"

#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 30'000;

using Progress = QcMsg::AdvancementProgress;

} // namespace

// The client side of the adventure guide (decision E-923): the progress model takes over snapshots
// and deltas (reset forgets, unknown advancements never count as reached, only announced reached
// ones ask for the toast), and a session on a local server hears of grants and revokes and reports
// them through the control interface.
class TestAdvancementClient : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    QTemporaryDir m_worldDirectory;
    std::unique_ptr<QcLocalServerHost> m_host;
    std::unique_ptr<QcClientSession> m_session;
    std::unique_ptr<QBuffer> m_output;
    std::unique_ptr<QcControlInterface> m_control;
    qsizetype m_readOffset = 0;

    void start()
    {
        QcServerConfig config;
        config.viewDistance = 2;
        config.worldName = u"Guide"_s;
        config.worldDirectory = m_worldDirectory.path();
        config.autosaveSeconds = 0;
        config.operators = {u"Bot"_s};
        m_host = std::make_unique<QcLocalServerHost>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_host->startError().isEmpty());
        m_session = std::make_unique<QcClientSession>(m_host->connectClient(), u"Bot"_s);
        m_output = std::make_unique<QBuffer>();
        m_output->open(QIODevice::ReadWrite);
        m_control = std::make_unique<QcControlInterface>(m_session.get(), nullptr, m_output.get());
        m_session->start();
        QTRY_VERIFY_WITH_TIMEOUT(m_session->state() == QcClientSession::State::Playing, timeoutMs);
    }

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
        return answered ? response : QJsonObject{};
    }

    QJsonObject reported(const QJsonObject &answer, const QString &name) const
    {
        const QJsonArray list = answer.value(u"advancements"_s).toArray();
        for (const auto &entry : list) {
            if (entry.toObject().value(u"name"_s).toString() == name) {
                return entry.toObject();
            }
        }
        return {};
    }

private slots:
    void cleanup()
    {
        m_control.reset();
        if (m_session) {
            m_session->disconnectFromServer();
            m_session.reset();
        }
        if (m_host) {
            QVERIFY(m_host->shutdown().has_value());
            m_host.reset();
        }
        m_readOffset = 0;
    }

    void modelResetsAndCountsOnlyKnownAdvancements()
    {
        QcClientAdvancements model;
        QCOMPARE(model.apply(Progress{true, {{u"qtcraft:iron"_s, {u"iron"_s}, true, false}}}, m_content.get()), QStringList{});
        QVERIFY(model.isDone(u"qtcraft:iron"_s));

        // Announced and reached: the toast; announced but unknown to the content: nothing.
        const QStringList announced = model.apply(Progress{false, {{u"qtcraft:map"_s, {u"map"_s}, true, true},
                                                                   {u"othermod:gone"_s, {u"x"_s}, true, true},
                                                                   {u"qtcraft:diamonds"_s, {}, false, true}}},
                                                  m_content.get());
        QCOMPARE(announced, QStringList{u"qtcraft:map"_s});
        QVERIFY(!model.isDone(u"othermod:gone"_s));
        QCOMPARE(model.doneCount(), 2);
        // The progress itself keeps what the server said, also of unknown advancements.
        QVERIFY(model.progress().has(u"othermod:gone"_s, u"x"_s));

        // A revoke (no criteria) and a reset forget.
        model.apply(Progress{false, {{u"qtcraft:map"_s, {}, false, false}}}, m_content.get());
        QVERIFY(!model.isDone(u"qtcraft:map"_s));
        model.apply(Progress{true, {}}, m_content.get());
        QCOMPARE(model.doneCount(), 0);
        QVERIFY(model.progress().isEmpty());
        // Without content nothing counts as reached.
        QCOMPARE(model.apply(Progress{false, {{u"qtcraft:iron"_s, {u"iron"_s}, true, true}}}, nullptr), QStringList{});
        QVERIFY(!model.isDone(u"qtcraft:iron"_s));
    }

    void sessionHearsGrantsAndRevokes()
    {
        start();
        QSignalSpy reached(m_session.get(), &QcClientSession::advancementReached);
        m_session->sendChat(u"/advancement grant Bot iron"_s);
        QTRY_COMPARE_WITH_TIMEOUT(reached.size(), 1, timeoutMs);
        QCOMPARE(reached.constFirst().constFirst().toString(), u"qtcraft:iron"_s);
        QVERIFY(m_session->advancements().isDone(u"qtcraft:iron"_s));

        const QJsonObject answer = request({{u"cmd"_s, u"advancements"_s}});
        QVERIFY(answer.value(u"ok"_s).toBool());
        QCOMPARE(answer.value(u"reached"_s).toInt(), 1);
        const QJsonObject iron = reported(answer, u"qtcraft:iron"_s);
        QCOMPARE(iron.value(u"done"_s).toBool(), true);
        QCOMPARE(iron.value(u"parent"_s).toString(), u"qtcraft:furnace"_s);
        QCOMPARE(iron.value(u"frame"_s).toString(), u"task"_s);
        QCOMPARE(iron.value(u"met"_s).toArray(), QJsonArray{u"iron"_s});
        QCOMPARE(answer.value(u"advancements"_s).toArray().size(), m_content->advancements().items().size());

        // A revoke is no toast.
        m_session->sendChat(u"/advancement revoke Bot iron"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!m_session->advancements().isDone(u"qtcraft:iron"_s), timeoutMs);
        QCOMPARE(reached.size(), 1);
        QCOMPARE(request({{u"cmd"_s, u"advancements"_s}}).value(u"reached"_s).toInt(), 0);
    }
};

QTEST_MAIN(TestAdvancementClient)
#include "test_advancement_client.moc"

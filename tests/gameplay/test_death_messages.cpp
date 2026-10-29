// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/protocol/QcChatText.h"
#include "common/protocol/QcProtocol.h"
#include "QcPlayerUseFixture.h"
#include "QcTestUtil.h"
#include "server/mobs/QcMobManager.h"
#include "server/projectiles/QcProjectileManager.h"

#include <QTemporaryDir>

#include <algorithm>

using namespace Qt::StringLiterals;

// Death messages with the creature that killed the player (decision E-873): a killing blow or shot
// names its kind as a content text (the client shows it in its language) or a named creature by its
// name; other deaths keep their lines. The content keys travel with the chat line. Named creatures
// stay: a witch (an ordinary monster of the hut's spawn area, decision E-871) named with a name tag
// is kept and comes back after a restart.
class TestDeathMessages : public QcPlayerUseFixture
{
    Q_OBJECT

private:
    // A creature of `type` at `pos`.
    quint32 spawnAt(const QString &type, QcBlockPos pos)
    {
        return m_server->mobs().spawn(*m_content->mobIndex(type), {pos.x + 0.5, static_cast<double>(pos.y), pos.z + 0.5}, m_server->entities().newId());
    }

    // A survival player with one health point at night, in the open; the creatures come later.
    void joinWeak()
    {
        join();
        chat(u"/gamerule mobSpawning false"_s);
        chat(u"/gamerule daylightCycle false"_s);
        chat(u"/time set 18000"_s);
        m_server->mobs().removeAll();
        player().health = 1.0;
    }

    // Ticks until the player died, at most `limit`; whether he did.
    bool waitForDeath(int limit)
    {
        for (int tick = 0; tick < limit && !player().dead; ++tick) {
            ticks(1);
            player().health = std::min(player().health, 1.0);
        }
        QcTestUtil::pump();
        return player().dead;
    }

    // The death line the player heard last.
    QcMsg::ChatMessage deathLine() const
    {
        for (qsizetype i = m_client->chatKeys.size() - 1; i >= 0; --i) {
            const QcMsg::ChatMessage &message = m_client->chatKeys[i];
            if (message.key.contains(u"died"_s) || message.key.contains(u" by %2"_s)) {
                return message;
            }
        }
        return {};
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void aBlowNamesTheCreaturesKind()
    {
        joinWeak();
        spawnAt(u"qtcraft:zombie"_s, m_feet.offset(1, 0, 0));
        QVERIFY(waitForDeath(400));
        const QcMsg::ChatMessage line = deathLine();
        QCOMPARE(line.key, u"%1 was slain by %2"_s);
        const QString zombie = m_content->mobs()[*m_content->mobIndex(u"qtcraft:zombie"_s)].translationKey;
        QCOMPARE(line.arguments, (QStringList{u"Raw"_s, m_content->translations().translate(zombie, u"en"_s)}));
        QCOMPARE(line.contentKeys, (QStringList{QString(), zombie}));
        // The client shows the kind in its language; without a text for the key, the English argument.
        const auto german = [this](const QString &key) { return m_content->translations().translate(key, u"de"_s); };
        QCOMPARE(QcChatText::display(line, german), u"Raw was slain by %1"_s.arg(german(zombie)));
        QCOMPARE(QcChatText::display(line, [](const QString &) { return QString(); }), u"Raw was slain by Zombie"_s);
    }

    void aNamedCreatureIsNamed()
    {
        joinWeak();
        const quint32 zombie = spawnAt(u"qtcraft:zombie"_s, m_feet.offset(1, 0, 0));
        m_server->mobs().find(zombie)->name = u"Bob"_s;
        QVERIFY(waitForDeath(400));
        const QcMsg::ChatMessage line = deathLine();
        QCOMPARE(line.key, u"%1 was slain by %2"_s);
        QCOMPARE(line.arguments, (QStringList{u"Raw"_s, u"Bob"_s}));
        QVERIFY(line.contentKeys.isEmpty());
    }

    void aShotNamesTheArcher()
    {
        joinWeak();
        for (int dx = 1; dx <= 8; ++dx) {
            m_server->world().setBlock(m_feet.offset(dx, -1, 0), id("qtcraft:stone"));
            m_server->world().setBlock(m_feet.offset(dx, 0, 0), qcAirBlockId);
            m_server->world().setBlock(m_feet.offset(dx, 1, 0), qcAirBlockId);
        }
        spawnAt(u"qtcraft:skeleton"_s, m_feet.offset(7, 0, 0));
        QVERIFY(waitForDeath(1200));
        const QcMsg::ChatMessage line = deathLine();
        QCOMPARE(line.key, u"%1 was shot by %2"_s);
        QCOMPARE(line.contentKeys.value(1), m_content->mobs()[*m_content->mobIndex(u"qtcraft:skeleton"_s)].translationKey);
    }

    void otherDeathsKeepTheirLines()
    {
        joinWeak();
        // A creature that hurt the player a while ago does not take the credit for a later death.
        player().foes.hurtBy = spawnAt(u"qtcraft:zombie"_s, m_feet.offset(3, 0, 3));
        player().foes.hurtTick = 1;
        m_server->mobs().find(player().foes.hurtBy)->noAi = true;
        chat(u"/kill"_s);
        ticks(1);
        QCOMPARE(deathLine().key, u"%1 died"_s);
        QVERIFY(deathLine().contentKeys.isEmpty());
    }

    void anArrowOfNoCreatureNamesNoEarlierAttacker()
    {
        // A zombie that hurt the player in this very tick does not take the credit for an arrow of
        // no creature (a dispenser's) that kills him in the same tick.
        joinWeak();
        const quint32 zombie = spawnAt(u"qtcraft:zombie"_s, m_feet.offset(3, 0, 3));
        m_server->mobs().find(zombie)->noAi = true;
        QcProjectileManager::Launch launch;
        launch.type = u"qtcraft:arrow"_s;
        launch.position = player().motion.position + QcVec3{0.0, 3.0, 0.0};
        launch.velocity = {0.0, -20.0, 0.0};
        QVERIFY(m_server->entities().projectiles().launch(launch, m_server->entities().newId()));
        for (int tick = 0; tick < 40 && !player().dead; ++tick) {
            // The tick about to run (the clock advances at its start).
            player().foes.hurtBy = zombie;
            player().foes.hurtTick = m_server->tickCount() + 1;
            ticks(1);
        }
        QcTestUtil::pump();
        QVERIFY(player().dead);
        QCOMPARE(deathLine().key, u"%1 died"_s);
        QVERIFY(deathLine().contentKeys.isEmpty());
    }

    void contentKeysTravelWithTheLine()
    {
        const QcMsg::ChatMessage line = QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "%1 was slain by %2"), {u"Raw"_s, u"Zombie"_s},
                                                            {QString(), u"mob.qtcraft.zombie"_s});
        const QcResult<QcMessage> back = QcProtocol::decode(QcProtocol::encode(QcMessage(line)), QcProtocol::Peer::Server);
        QVERIFY(back.has_value());
        const auto *chat = std::get_if<QcMsg::ChatMessage>(&*back);
        QVERIFY(chat);
        QCOMPARE(chat->contentKeys, line.contentKeys);
        QCOMPARE(chat->arguments, line.arguments);

        // Keys not as many as the arguments are refused.
        QcMsg::ChatMessage broken = line;
        broken.contentKeys.append(u"mob.qtcraft.skeleton"_s);
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMessage(broken)), QcProtocol::Peer::Server).has_value());
    }

    void aNamedWitchIsKeptAcrossARestart()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        config.autosaveSeconds = 0;
        join(config);
        chat(u"/gamerule mobSpawning false"_s);
        m_server->mobs().removeAll();
        const quint32 witch = spawnAt(u"qtcraft:witch"_s, m_feet.offset(2, 0, 0));
        m_server->mobs().find(witch)->noAi = true;
        QVERIFY(!m_server->mobs().find(witch)->persistent);

        hold(u"name_tag"_s);
        player().inventory.setSlot(player().inventory.selectedHotbarSlot(), QcItemStack{item("qtcraft:name_tag"), 1, 0}.withName(u"Agatha"_s));
        send(QcMsg::UseEntity{witch});
        QVERIFY(m_server->mobs().find(witch)->persistent);

        QVERIFY(m_server->stop().has_value());
        m_client.reset();
        config.viewDistance = 1;
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());
        loadAround(m_feet);
        const auto named = std::ranges::find(m_server->mobs().mobs(), u"Agatha"_s, &QcCreature::name);
        QVERIFY(named != m_server->mobs().mobs().end());
        QCOMPARE(named->type, *m_content->mobIndex(u"qtcraft:witch"_s));
        QVERIFY(named->persistent);
    }
};

QTEST_GUILESS_MAIN(TestDeathMessages)
#include "test_death_messages.moc"

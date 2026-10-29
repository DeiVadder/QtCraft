// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/mobs/QcMobParser.h"
#include "common/content/mobs/QcMobTraitsContent.h"
#include "common/content/registry/QcContentSerializer.h"
#include "gameplay/QcPetFixture.h"
#include "QcTestUtil.h"
#include "server/mobbrains/QcTameBrain.h"
#include "server/storage/QcEntityCodec.h"
#include "server/storage/QcEntityRecords.h"

#include <QJsonDocument>

#include <numbers>

using namespace Qt::StringLiterals;

// Taming as a reusable behaviour (decision E-480): the data of wolves, dogs, cats and ocelots, taming
// with a chance, the owner by identity, sitting, following, teleporting to the owner (only into
// loaded chunks, never while sitting), the owner offline and back, collars, healing, and saving the
// ownership. Ticks are stepped by hand.
class TestTaming : public QcPetFixture
{
    Q_OBJECT

private:
    // A second player "Other" joined to the server; its server side is the last client.
    std::unique_ptr<RawClient> joinOther()
    {
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        auto other = std::make_unique<RawClient>(std::move(clientEnd));
        other->hello(u"Other"_s);
        QcTestUtil::pump();
        ticks(2);
        return other;
    }

    // Saves the chunk of the creature through the entity codec and loads it again; the creature that
    // came back.
    const QcCreature *saveAndLoad(quint32 creature)
    {
        const QcChunkPos chunk = QcEntities::chunkOf(*mob(creature));
        const int kindOf = mob(creature)->type;
        const QHash<QcChunkPos, QcEntityRecord> records = QcEntityRecords::take(m_server->entities(), {chunk});
        QcLoadWarnings warnings;
        const QcResult<QcEntityRecord> back = QcEntityCodec::decode(QcEntityCodec::encode(records[chunk], *m_content), chunk, *m_content, warnings);
        QSet<quint32> before;
        for (const QcCreature &one : m_server->entities().creatures().mobs()) {
            before.insert(one.id);
        }
        if (!back.has_value() || !QcEntityRecords::apply(*back, m_server->entities()).isEmpty()) {
            return nullptr;
        }
        const auto loaded = std::ranges::find_if(m_server->entities().creatures().mobs(), [&](const QcCreature &one) {
            return one.type == kindOf && !before.contains(one.id);
        });
        return loaded == m_server->entities().creatures().mobs().end() ? nullptr : &*loaded;
    }

    // The farthest a pet lands from its owner after a teleport, horizontally (a corner of the range).
    static constexpr double nearOwner = QcTameBrain::teleportRange * std::numbers::sqrt2 + 0.5;
    static double flat(const QcVec3 &a, const QcVec3 &b) { return std::hypot(a.x - b.x, a.z - b.z); }

    static std::optional<QString> parseError(const QByteArray &tameable)
    {
        const QByteArray json = R"({"name": "x", "health": 4, "ai": "neutral", "tameable": )" + tameable
                                + R"(, "model": [{"from": [0, 0, 0], "to": [1, 1, 1], "texture": "x"}]})";
        const QcResult<QcMobDefinition> parsed = QcMobParser::parseMob(QJsonDocument::fromJson(json).object(), u"qtcraft"_s);
        return parsed ? std::nullopt : std::optional<QString>(parsed.error());
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
        m_content = QcTestUtil::loadContent();
    }

    void wolvesDogsCatsAndOcelotsComeFromData()
    {
        const QcMobDefinition &wolf = kind("wolf");
        QVERIFY(wolf.tameable);
        QCOMPARE(wolf.tameable->items, QStringList{u"qtcraft:bone"_s});
        QCOMPARE(wolf.tameable->becomesType, type("dog"));
        QCOMPARE(wolf.ai, QcMobDefinition::Ai::Neutral);
        QVERIFY(wolf.packRange > 0.0);
        QVERIFY(wolf.spawnBiomes.contains(u"qtcraft:taiga"_s) && wolf.spawnBiomes.contains(u"qtcraft:forest"_s));

        const QcMobDefinition &dog = kind("dog");
        QVERIFY(dog.tameable && dog.tameable->defends && dog.tameable->hasCollar());
        QCOMPARE(dog.tameable->collarColor(dog.tameable->collarDefault), std::optional<quint32>(0xb02e26));
        QVERIFY(dog.tameable->healItems.contains(u"qtcraft:raw_porkchop"_s));
        QVERIFY(std::ranges::any_of(dog.parts, [](const QcMobPart &part) { return part.tint == QcTint::Collar; }));
        QVERIFY(std::ranges::any_of(dog.parts, [](const QcMobPart &part) { return part.sitOffset.length() > 0.0; }));

        QVERIFY(kind("cat").tameable && !kind("cat").tameable->defends && kind("cat").frightens);
        QCOMPARE(kind("cat").tameable->items, QStringList{u"qtcraft:raw_cod"_s});
        QVERIFY(kind("ocelot").shy && !kind("ocelot").tameable);
        QVERIFY(m_content->itemId(u"qtcraft:cooked_cod"_s));
    }

    void badTamingDataIsRejected()
    {
        QVERIFY(!parseError(R"({"items": ["bone"]})"));
        QVERIFY(parseError(R"({"items": ["bone"], "stopDistance": 8, "followDistance": 6})")->contains(u"stopDistance"_s));
        QVERIFY(parseError(R"({"collar": {"default": "red_dye", "dyes": {"blue_dye": "#0000ff"}}})")->contains(u"default"_s));
        QVERIFY(parseError(R"({"collar": {"default": "red_dye", "dyes": {"red_dye": "red"}}})")->contains(u"#rrggbb"_s));
    }

    void structureOwnersNeedATameableKind()
    {
        QCOMPARE(QcMobTraitsContent::checkStructurePets(*m_content), QStringList());
        QcContentRegistry content(*m_content);
        QcStructureDefinition pen;
        pen.name = u"qtcraft:test_pen"_s;
        pen.mobs = {{.mob = u"qtcraft:pig"_s, .at = {}, .owner = u"showcase"_s}, {.mob = u"qtcraft:cat"_s, .at = {}, .owner = u"showcase"_s}};
        content.worldgen().registerStructure(pen);
        const QStringList warnings = QcMobTraitsContent::checkStructurePets(content);
        QCOMPARE(warnings.size(), 1);
        QVERIFY(warnings.front().contains(u"qtcraft:pig"_s));
    }

    void tamingSucceedsWithItsChance()
    {
        joinArena();
        // Each bone tames with a third: of 60 wolves fed one bone each, about 20 (fixed seed).
        int tamed = 0;
        hold("qtcraft:bone");
        for (int i = 0; i < 60; ++i) {
            const quint32 wolf = spawn("wolf", 0, 0);
            use(wolf);
            tamed += mob(wolf)->isTame() ? 1 : 0;
            m_server->mobs().remove(wolf);
        }
        QVERIFY2(tamed >= 10 && tamed <= 32, qPrintable(QString::number(tamed)));
        QCOMPARE(heldCount(), 64 - 60);
    }

    void aTamedWolfIsTheOwnersSittingDog()
    {
        setTameChance("wolf", 1.0);
        joinArena();
        const quint32 wolf = spawn("wolf", 3, 0);
        hold("qtcraft:bone", 5);
        use(wolf);
        QCOMPARE(mob(wolf)->type, type("dog"));
        QVERIFY(mob(wolf)->isTame());
        // Owned by the identity the player proved at the login, not by the entity id.
        QVERIFY(!player().login.fingerprint.isEmpty());
        QCOMPARE(mob(wolf)->tame->owner, player().login.fingerprint);
        QVERIFY(mob(wolf)->sits());
        QCOMPARE(mob(wolf)->pose, QcPose::Sitting);
        QVERIFY(mob(wolf)->persistent && mob(wolf)->uid != 0);
        QCOMPARE(mob(wolf)->tame->collar, 0xb02e26u);
        QCOMPARE(heldCount(), 4);

        // Another bone on the dog does nothing but stand it up (it is no healing food).
        use(wolf);
        QVERIFY(!mob(wolf)->sits());
        QCOMPARE(heldCount(), 4);
    }

    void chanceZeroNeverTames()
    {
        setTameChance("wolf", 0.0);
        joinArena();
        const quint32 wolf = spawn("wolf", 3, 0);
        hold("qtcraft:bone", 10);
        for (int i = 0; i < 10; ++i) {
            use(wolf);
        }
        QVERIFY(!mob(wolf)->isTame());
        QCOMPARE(mob(wolf)->type, type("wolf"));
        QCOMPARE(heldCount(), 0);
    }

    void theOwnerTogglesSittingOthersDoNot()
    {
        joinArena();
        const quint32 dog = pet("dog", 3, 0, true);
        hold(nullptr);
        use(dog);
        QVERIFY(!mob(dog)->sits());
        QCOMPARE(mob(dog)->pose, QcPose::Standing);
        use(dog);
        QVERIFY(mob(dog)->sits());

        // Another player's use does not make it stand.
        const std::unique_ptr<RawClient> other = joinOther();
        QcServerClient &stranger = *m_server->entities().clients().back();
        stranger.motion.position = mob(dog)->motion.position + QcVec3{0.0, 0.0, 2.0};
        stranger.pitch = 20.0f;
        other->connection.send(QcMsg::UseEntity{dog});
        QcTestUtil::pump();
        ticks(1);
        QVERIFY(mob(dog)->sits());

        // The stranger's use does reach the dog: their meat heals it (anyone may feed a pet).
        mob(dog)->health = 5.0;
        stranger.inventory.setSlot(stranger.inventory.selectedHotbarSlot(), QcItemStack{item("qtcraft:raw_porkchop"), 2, 0});
        other->connection.send(QcMsg::UseEntity{dog});
        QcTestUtil::pump();
        ticks(1);
        QCOMPARE(mob(dog)->health, 9.0);
        QVERIFY(mob(dog)->sits());
    }

    void creativeModeUsesUpNoTamingItems()
    {
        setTameChance("wolf", 1.0);
        joinArena();
        player().gameMode = QcGameMode::Creative;
        const quint32 wolf = spawn("wolf", 3, 0);
        hold("qtcraft:bone", 5);
        use(wolf);
        QVERIFY(mob(wolf)->isTame());
        QCOMPARE(heldCount(), 5);
        mob(wolf)->health = 5.0;
        hold("qtcraft:raw_porkchop", 2);
        use(wolf);
        QCOMPARE(mob(wolf)->health, 9.0);
        QCOMPARE(heldCount(), 2);
    }

    void angryWolvesCannotBeTamed()
    {
        setTameChance("wolf", 1.0);
        joinArena();
        const quint32 wolf = spawn("wolf", 3, 0);
        mob(wolf)->provokedBy = player().id;
        mob(wolf)->angerTimer = 30.0;
        hold("qtcraft:bone", 5);
        use(wolf);
        QVERIFY(!mob(wolf)->isTame());
        QCOMPARE(heldCount(), 5);
    }

    void tamingFreesAConfinedCreature()
    {
        setTameChance("cat", 1.0);
        joinArena();
        const quint32 cat = spawn("cat", 3, 0);
        mob(cat)->confined = true;
        hold("qtcraft:raw_cod", 2);
        use(cat);
        QVERIFY(mob(cat)->isTame());
        QVERIFY(!mob(cat)->confined);
    }

    void theLocalPlayerOwnsPetsByName()
    {
        // The local player of the in-process host logs in without a key: its pets belong to its name.
        setTameChance("wolf", 1.0);
        m_origin = QcServer::ClientOrigin::Local;
        joinArena();
        QVERIFY(player().login.fingerprint.isEmpty());
        QCOMPARE(ownerKey(), u"name:"_s + player().name.toCaseFolded());
        const quint32 wolf = spawn("wolf", 3, 0);
        hold("qtcraft:bone", 5);
        use(wolf);
        QVERIFY(mob(wolf)->isTame());
        QCOMPARE(mob(wolf)->tame->owner, u"name:"_s + player().name.toCaseFolded());
        // Its own use makes it stand: the name key is its owner's.
        hold(nullptr);
        use(wolf);
        QVERIFY(!mob(wolf)->sits());
    }

    void aHurtSittingPetStandsUp()
    {
        joinArena();
        const quint32 dog = pet("dog", 3, 0, true);
        const quint32 zombie = spawn("zombie", -3, 0);
        QVERIFY(!m_server->entities().creatures().damage({.creature = dog, .damage = 2.0, .from = mob(zombie)->motion.position}));
        QCOMPARE(mob(dog)->health, kind("dog").maxHealth - 2.0);
        QVERIFY(!mob(dog)->sits());
        ticks(1);
        QCOMPARE(mob(dog)->pose, QcPose::Standing);
    }

    void theClientLearnsTintsSitOffsetsAndTameKinds()
    {
        QByteArray bytes;
        {
            QDataStream out(&bytes, QIODevice::WriteOnly);
            QcContentSerializer::write(*m_content, out);
        }
        QDataStream in(bytes);
        const QcResult<QcContentRegistry> client = QcContentSerializer::read(in);
        QVERIFY(client.has_value());
        for (const char *name : {"dog", "wolf", "cat", "ocelot"}) {
            const QcMobDefinition &sent = kind(name);
            const QcMobDefinition &got = client->mobs()[*client->mobIndex(sent.name)];
            QCOMPARE(got.tameable.has_value(), sent.tameable.has_value());
            QCOMPARE(got.parts.size(), sent.parts.size());
            for (qsizetype i = 0; i < sent.parts.size(); ++i) {
                QCOMPARE(got.parts[i].tint, sent.parts[i].tint);
                QCOMPARE(got.parts[i].sitOffset, sent.parts[i].sitOffset);
            }
        }
        QVERIFY(!client->mobs()[*client->mobIndex(u"qtcraft:ocelot"_s)].tameable);
        const QList<QcMobPart> &dog = client->mobs()[*client->mobIndex(u"qtcraft:dog"_s)].parts;
        QVERIFY(std::ranges::any_of(dog, [](const QcMobPart &part) { return part.tint == QcTint::Collar; }));
        QVERIFY(std::ranges::any_of(dog, [](const QcMobPart &part) { return part.sitOffset.length() > 0.0; }));
    }

    void aPetFollowsBeyondItsDistanceUntilClose()
    {
        joinArena();
        const quint32 dog = pet("dog", 0, 4);
        player().motion.position = at(0, 0);
        ticks(40);
        // Within the follow distance it stays where it is.
        QVERIFY(std::abs(distance(dog) - 4.0) < 0.5);

        player().motion.position = at(0, -6);
        ticks(100);
        QVERIFY2(distance(dog) <= kind("dog").tameable->stopDistance + 1.0, qPrintable(QString::number(distance(dog))));
        QVERIFY(!mob(dog)->teleportedFrom);
    }

    void aFarOwnerIsReachedByTeleportButNotWhileSitting()
    {
        joinArena();
        const quint32 dog = pet("dog", 0, 0);
        const quint32 sitting = pet("dog", 1, 0, true);
        player().motion.position = at(0, 20);
        ticks(1);
        QVERIFY(flat(mob(dog)->motion.position, player().motion.position) <= nearOwner);
        QVERIFY(distance(sitting) > 18.0);
    }

    void teleportsGoOnlyIntoLoadedChunks()
    {
        joinArena();
        const quint32 dog = pet("dog", 0, 0);
        QcCreature &creature = *mob(dog);
        const QcMobDefinition &dogKind = kind("dog");
        QRandomGenerator random(5);
        // Far beyond the loaded chunks there is no place, and the dog stays.
        const QcVec3 before = creature.motion.position;
        const QcVec3 unloaded{m_feet.x + 4000.5, static_cast<double>(m_feet.y), m_feet.z + 0.5};
        QVERIFY(!m_server->world().chunk(QcPhysics::blockContaining(unloaded).chunk()));
        QVERIFY(!QcTameBrain::teleportNear(creature, dogKind, m_server->world(), random, unloaded));
        QCOMPARE(creature.motion.position, before);
        // Around a loaded place it lands within the range, on the floor.
        QVERIFY(QcTameBrain::teleportNear(creature, dogKind, m_server->world(), random, at(10, 10)));
        QVERIFY(flat(creature.motion.position, at(10, 10)) <= nearOwner);
        QCOMPARE(creature.motion.position.y, double(m_feet.y));

        // An owner beyond the loaded chunks: the pet stays where it is.
        player().motion.position = unloaded;
        creature.teleportedFrom.reset();
        const QcVec3 standing = mob(dog)->motion.position;
        ticks(1);
        QVERIFY((mob(dog)->motion.position - standing).length() < 0.5);
    }

    void aPetStaysWhileItsOwnerIsOfflineAndFollowsWhenBack()
    {
        joinArena();
        std::unique_ptr<RawClient> other = joinOther();
        QcServerClient &owner = *m_server->entities().clients().back();
        const QString key = QcPetOwners::keyOf(owner);
        const quint32 dog = spawn("dog", 0, 4);
        QcPetCare::tame(m_server->entities(), *m_content, *mob(dog), key);
        mob(dog)->tame->sitting = false;
        owner.motion.position = at(0, 0);
        ticks(5);

        // The owner leaves; the other player walks far away: the dog stays.
        other.reset();
        QcTestUtil::pump();
        ticks(2);
        QCOMPARE(m_server->clientCount(), 1);
        const QcVec3 stay = mob(dog)->motion.position;
        player().motion.position = at(0, -20);
        ticks(60);
        QVERIFY((mob(dog)->motion.position - stay).length() < 0.5);
        QCOMPARE(mob(dog)->tame->owner, key);

        // Back with the same identity: it comes to its owner.
        other = joinOther();
        QcServerClient &back = *m_server->entities().clients().back();
        QCOMPARE(QcPetOwners::keyOf(back), key);
        back.motion.position = at(0, 20);
        ticks(2);
        QVERIFY(flat(mob(dog)->motion.position, back.motion.position) <= nearOwner);
    }

    void dyesColourTheCollarAndMeatHeals()
    {
        joinArena();
        const quint32 dog = pet("dog", 3, 0, true);
        hold("qtcraft:blue_dye", 3);
        use(dog);
        QCOMPARE(mob(dog)->tame->collar, 0x3c44aau);
        QCOMPARE(heldCount(), 2);
        QVERIFY(mob(dog)->sits());
        // The same colour again uses no dye.
        use(dog);
        QCOMPARE(heldCount(), 2);

        mob(dog)->health = 5.0;
        hold("qtcraft:raw_porkchop", 2);
        use(dog);
        QCOMPARE(mob(dog)->health, 9.0);
        QCOMPARE(heldCount(), 1);
    }

    void catsTameWithFishAndKeepTheirKind()
    {
        setTameChance("cat", 1.0);
        joinArena();
        const quint32 cat = spawn("cat", 3, 0);
        hold("qtcraft:raw_cod", 2);
        use(cat);
        QCOMPARE(mob(cat)->type, type("cat"));
        QVERIFY(mob(cat)->isTame());
        QCOMPARE(mob(cat)->tame->owner, ownerKey());
        QCOMPARE(heldCount(), 1);
        // A bone does not tame a cat.
        const quint32 stray = spawn("cat", -3, 0);
        hold("qtcraft:bone", 4);
        use(stray);
        QVERIFY(!mob(stray)->isTame());
    }

    void ownershipIsSavedAndLoaded()
    {
        joinArena();
        const quint32 dog = pet("dog", 3, 0, true);
        mob(dog)->tame->collar = 0x3c44aa;
        const QString owner = mob(dog)->tame->owner;
        const quint64 uid = mob(dog)->uid;
        const QcCreature *loaded = saveAndLoad(dog);
        QVERIFY(loaded);
        QVERIFY(loaded->isTame());
        QCOMPARE(loaded->tame->owner, owner);
        QVERIFY(loaded->tame->sitting);
        QCOMPARE(loaded->tame->collar, 0x3c44aau);
        QCOMPARE(loaded->pose, QcPose::Sitting);
        QCOMPARE(loaded->uid, uid);

        // A wild wolf has no tame component in its entry and stays wild.
        const quint32 wolf = spawn("wolf", -3, 0);
        m_server->mobs().keep(*mob(wolf), true);
        const QcCreature *wild = saveAndLoad(wolf);
        QVERIFY(wild && !wild->tame);
    }
};

QTEST_GUILESS_MAIN(TestTaming)
#include "test_taming.moc"

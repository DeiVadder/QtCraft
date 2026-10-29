// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcClientTrading.h"
#include "common/content/villages/QcTradeParser.h"
#include "common/protocol/QcProtocol.h"
#include "common/world/QcDayCycle.h"
#include "QcTestUtil.h"
#include "server/storage/QcVillagerCodec.h"
#include "server/villages/QcVillagerTrades.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;

// The offers of the villagers without a server (decision E-414): which offers a villager has by
// profession, level and look, prices with demand, levels, restocking by day and gap, the data
// format and its refusals, the saved state and the trade messages.
class TestTradeOffers : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    QcVillagerTrades m_trades{*m_content, *m_content->villageCatalog().villagerSettings(u"qtcraft:villager"_s)};

    static QcVillagerRecord villagerOf(const QString &profession, int level, quint32 look = 7)
    {
        QcVillagerRecord villager;
        villager.profession = profession;
        villager.level = level;
        villager.look = look;
        return villager;
    }

    static QJsonObject parse(const char *json) { return QJsonDocument::fromJson(json).object(); }

private slots:
    void everyOfferOfTheBaseGameResolves()
    {
        // Each trade file of the base game: all its offers reach the catalog (no unknown item).
        for (const QcTradeSet &set : m_content->villageCatalog().tradeSets()) {
            QCOMPARE(m_trades.offersOfProfession(set.profession).size(), set.offers.size());
            for (int level = 1; level <= QcTradingSettings::levelCount; ++level) {
                QVERIFY2(std::ranges::any_of(set.offers, [level](const QcTradeOffer &offer) { return offer.level == level; }),
                         qPrintable(u"%1 has no offer of level %2"_s.arg(set.name).arg(level)));
            }
        }
        QCOMPARE(m_content->villageCatalog().tradeSets().size(), 3);
    }

    void aVillagerHasOffersByProfessionAndLevel()
    {
        const int perLevel = m_trades.settings().offersPerLevel;
        for (const QString &profession : {u"qtcraft:farmer"_s, u"qtcraft:smith"_s, u"qtcraft:fisher"_s}) {
            for (int level = 1; level <= 5; ++level) {
                const QList<const QcVillagerTrades::Offer *> offers = m_trades.offersOf(villagerOf(profession, level));
                QCOMPARE(offers.size(), perLevel * level);
                // Level by level, each offer of a level it reached, none twice.
                for (qsizetype i = 0; i < offers.size(); ++i) {
                    QCOMPARE(offers[i]->definition.level, int(i / perLevel) + 1);
                    QCOMPARE(std::ranges::count(offers, offers[i]), 1);
                }
            }
        }
        QVERIFY(m_trades.offersOf(villagerOf(u"qtcraft:unemployed"_s, 1)).isEmpty());
        QVERIFY(m_trades.offersOf(villagerOf(u"qtcraft:nobody"_s, 3)).isEmpty());
    }

    void theLookDecidesWhichOffersForGood()
    {
        // The same look draws the same offers; a level more keeps the earlier ones; looks differ.
        const QList<const QcVillagerTrades::Offer *> first = m_trades.offersOf(villagerOf(u"qtcraft:farmer"_s, 2, 11));
        QCOMPARE(m_trades.offersOf(villagerOf(u"qtcraft:farmer"_s, 2, 11)), first);
        QCOMPARE(m_trades.offersOf(villagerOf(u"qtcraft:farmer"_s, 3, 11)).mid(0, first.size()), first);
        QSet<QString> drawn;
        for (quint32 look = 0; look < 32; ++look) {
            drawn.insert(m_trades.offersOf(villagerOf(u"qtcraft:farmer"_s, 1, look)).constFirst()->definition.id);
        }
        QVERIFY(drawn.size() > 1);
    }

    void syncKeepsKnownOffersAndDropsOthers()
    {
        QcVillagerRecord villager = villagerOf(u"qtcraft:smith"_s, 1);
        const QList<const QcVillagerTrades::Offer *> offers = m_trades.offersOf(villager);
        villager.offers = {{offers[1]->definition.id, 3, 2}, {u"qtcraft:gone"_s, 5, 1}};
        m_trades.sync(villager);
        QCOMPARE(villager.offers.size(), offers.size());
        QCOMPARE(villager.offers[0], (QcVillagerOffer{offers[0]->definition.id, 0, 0}));
        QCOMPARE(villager.offers[1], (QcVillagerOffer{offers[1]->definition.id, 3, 2}));

        // Another profession: nothing of the smith stays.
        villager.profession = u"qtcraft:unemployed"_s;
        m_trades.sync(villager);
        QVERIFY(villager.offers.isEmpty());
    }

    void demandRaisesThePriceUpToTwice()
    {
        QcVillagerTrades::Offer offer;
        offer.definition.demand = 3;
        offer.costs = {{static_cast<QcItemId>(1), 10, 0}, {static_cast<QcItemId>(2), 1, 0}};
        offer.firstMaxStack = 64;
        QCOMPARE(m_trades.priceOf(offer, {u"x"_s, 0, 0}).constFirst().count, 10);
        QCOMPARE(m_trades.priceOf(offer, {u"x"_s, 0, 2}).constFirst().count, 16);
        QCOMPARE(m_trades.priceOf(offer, {u"x"_s, 0, 9}).constFirst().count, 20);
        QCOMPARE(m_trades.priceOf(offer, {u"x"_s, 0, 9}).constLast().count, 1);
        offer.firstMaxStack = 16;
        QCOMPARE(m_trades.priceOf(offer, {u"x"_s, 0, 9}).constFirst().count, 16);
        offer.definition.demand = 0;
        QCOMPARE(m_trades.priceOf(offer, {u"x"_s, 0, 9}).constFirst().count, 10);
    }

    void experienceRaisesTheLevel()
    {
        QcVillagerRecord villager = villagerOf(u"qtcraft:farmer"_s, 1);
        QCOMPARE(m_trades.levelRange(1), std::pair(0, 10));
        QCOMPARE(m_trades.levelRange(5), std::pair(250, 250));
        QCOMPARE(m_trades.addExperience(villager, 9), 0);
        QCOMPARE(m_trades.addExperience(villager, 1), 1);
        QCOMPARE(villager.level, 2);
        QCOMPARE(m_trades.addExperience(villager, 500), 3);
        QCOMPARE(villager.level, 5);
        QCOMPARE(m_trades.addExperience(villager, 500), 0);
        QCOMPARE(villager.level, 5);
        QCOMPARE(m_trades.addExperience(villager, -50), 0);
        QCOMPARE(villager.experience, 1010);
    }

    void restocksTwiceADayWithAGap()
    {
        QcVillagerRecord villager = villagerOf(u"qtcraft:farmer"_s, 1);
        m_trades.sync(villager);
        const double day = QcDayCycle::dayLength;
        const double morning = day * 20.1;
        QVERIFY(!m_trades.restock(villager, morning));

        villager.offers[0].uses = 16;
        villager.offers[1].uses = 2;
        QVERIFY(m_trades.restock(villager, morning));
        QCOMPARE(villager.offers[0].uses, 0);
        QCOMPARE(villager.offers[0].demand, 1);
        QCOMPARE(villager.offers[1].demand, 0);

        // Too soon after the first, then the second of the day, then none until the next day.
        villager.offers[1].uses = 1;
        QVERIFY(!m_trades.restock(villager, morning + day * 0.05));
        QVERIFY(m_trades.restock(villager, morning + day * 0.1));
        QCOMPARE(villager.offers[0].demand, 0);
        villager.offers[1].uses = 1;
        QVERIFY(!m_trades.restock(villager, morning + day * 0.5));
        QVERIFY(m_trades.restock(villager, morning + day * 1.0));
        QCOMPARE(villager.restocks, 1);
    }

    void aModAddingAnOfferKeepsTheVillagersOffers()
    {
        // A mod adds an offer to each level of the farmer, and replaces one by its id.
        QcContentRegistry modded = *m_content;
        QcTradeSet set{u"more_farming"_s, u"qtcraft:farmer"_s, {}};
        for (int level = 1; level <= QcTradingSettings::levelCount; ++level) {
            set.offers.append({u"mod:offer%1"_s.arg(level), level, {{u"qtcraft:dirt"_s, 1}}, {u"qtcraft:emerald"_s, 1}, 12, 2, 1, 0});
        }
        const QcTradeOffer replaced = m_trades.offersOfProfession(u"qtcraft:farmer"_s).constFirst().definition;
        set.offers.append({replaced.id, replaced.level, {{u"qtcraft:dirt"_s, 3}}, replaced.result, 5, 2, 1, 0});
        modded.villageCatalog().registerTradeSet(set);
        const QcVillagerTrades moddedTrades(modded, *modded.villageCatalog().villagerSettings(u"qtcraft:villager"_s));

        // At catalog level the replacement keeps its place, and the new offers are there.
        const QList<QcVillagerTrades::Offer> farmer = moddedTrades.offersOfProfession(u"qtcraft:farmer"_s);
        QCOMPARE(farmer.size(), m_trades.offersOfProfession(u"qtcraft:farmer"_s).size() + QcTradingSettings::levelCount);
        QCOMPARE(farmer.constFirst().definition.id, replaced.id);
        QCOMPARE(farmer.constFirst().definition.maxUses, 5);
        QCOMPARE(farmer.constFirst().costs.constFirst().count, 3);

        // Every existing villager keeps the offers it had.
        for (quint32 look = 0; look < 32; ++look) {
            QcVillagerRecord villager = villagerOf(u"qtcraft:farmer"_s, 5, look);
            m_trades.sync(villager);
            const QList<QcVillagerOffer> before = villager.offers;
            moddedTrades.sync(villager);
            QCOMPARE(villager.offers, before);
        }

        // New villagers may draw the new offers; the draw does not depend on the generator of Qt.
        QSet<QString> drawn;
        for (quint32 look = 0; look < 64; ++look) {
            const QList<const QcVillagerTrades::Offer *> offers = moddedTrades.offersOf(villagerOf(u"qtcraft:farmer"_s, 1, look));
            for (const QcVillagerTrades::Offer *offer : offers) {
                drawn.insert(offer->definition.id);
            }
        }
        QVERIFY(drawn.contains(u"mod:offer1"_s));
    }

    void anOfferRemovedByAModIsReplacedByAnother()
    {
        // The removed offer's place is drawn anew; the other offer of the level stays.
        QcVillagerRecord villager = villagerOf(u"qtcraft:smith"_s, 1);
        m_trades.sync(villager);
        const QString kept = villager.offers[1].id;
        villager.offers[0].id = u"mod:gone"_s;
        m_trades.sync(villager);
        QCOMPARE(villager.offers.size(), m_trades.settings().offersPerLevel);
        QVERIFY(std::ranges::find(std::as_const(villager.offers), kept, &QcVillagerOffer::id) != villager.offers.cend());
        QVERIFY(std::ranges::find(std::as_const(villager.offers), u"mod:gone"_s, &QcVillagerOffer::id) == villager.offers.cend());
    }

    void aHugeWorldTimeRestocksWithoutOverflow()
    {
        QcVillagerRecord villager = villagerOf(u"qtcraft:farmer"_s, 1);
        m_trades.sync(villager);
        // Days are counted from a bounded time: the first restock happens, a second at once is too soon.
        villager.offers[0].uses = 1;
        QVERIFY(m_trades.restock(villager, 1e300));
        QCOMPARE(villager.lastRestock, 1e300);
        villager.offers[0].uses = 1;
        QVERIFY(!m_trades.restock(villager, 1e300));
        QCOMPARE(villager.restocks, 1);
    }

    void aClosedScreenIgnoresAStateStillOnItsWay()
    {
        QcClientTrading trading;
        QList<QcMessage> sent;
        trading.setSender([&sent](const QcMessage &message) { sent.append(message); });
        const QcMsg::TradeState state{12, u"profession.qtcraft.smith"_s, 1, 0, 0, 10, {}};
        QVERIFY(trading.handle(state));
        QVERIFY(trading.isOpen());

        // The player closes; the answer to an earlier trade arrives afterwards and is ignored.
        trading.close();
        QCOMPARE(sent.size(), 1);
        QVERIFY(std::holds_alternative<QcMsg::CloseTrade>(sent.constFirst()));
        QVERIFY(trading.handle(state));
        QVERIFY(!trading.isOpen());

        // Using a villager again lets its screen open.
        trading.usedEntity();
        QVERIFY(trading.handle(state));
        QVERIFY(trading.isOpen());
    }

    void tradeFilesAreParsed()
    {
        const QcResult<QcTradeSet> set = QcTradeParser::parseTradeSet(parse(R"({"name": "baker", "profession": "baker", "offers": [
            {"level": 2, "costs": [{"item": "wheat", "count": 9}, {"item": "sugar"}], "result": {"item": "bread", "count": 3},
             "maxUses": 5, "villagerExperience": 7, "playerExperience": 4, "demand": 1},
            {"id": "second", "costs": [{"item": "mod:flour"}], "result": {"item": "emerald"}}]})"),
                                                                     u"qtcraft"_s);
        QVERIFY2(set.has_value(), qPrintable(set ? QString() : set.error()));
        QCOMPARE(set->profession, u"qtcraft:baker"_s);
        QCOMPARE(set->offers.size(), 2);
        const QcTradeOffer &first = set->offers[0];
        QCOMPARE(first.id, u"qtcraft:wheat>qtcraft:bread"_s);
        QCOMPARE(first.costs, (QList<QcTradeStack>{{u"qtcraft:wheat"_s, 9}, {u"qtcraft:sugar"_s, 1}}));
        QCOMPARE(first.result, (QcTradeStack{u"qtcraft:bread"_s, 3}));
        QCOMPARE(std::tuple(first.level, first.maxUses, first.villagerExperience, first.playerExperience, first.demand), std::tuple(2, 5, 7, 4, 1));
        QCOMPARE(set->offers[1].costs.constFirst().item, u"mod:flour"_s);
        QCOMPARE(set->offers[1].level, 1);
    }

    void brokenTradeFilesAreRefused_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("no profession") << QByteArray(R"({"name": "a", "offers": [{"costs": [{"item": "x"}], "result": {"item": "y"}}]})");
        QTest::newRow("no offers") << QByteArray(R"({"name": "a", "profession": "p", "offers": []})");
        QTest::newRow("three costs") << QByteArray(
            R"({"name": "a", "profession": "p", "offers": [{"costs": [{"item": "x"}, {"item": "y"}, {"item": "z"}], "result": {"item": "y"}}]})");
        QTest::newRow("no result") << QByteArray(R"({"name": "a", "profession": "p", "offers": [{"costs": [{"item": "x"}]}]})");
        QTest::newRow("level 6") << QByteArray(
            R"({"name": "a", "profession": "p", "offers": [{"level": 6, "costs": [{"item": "x"}], "result": {"item": "y"}}]})");
        QTest::newRow("count 0") << QByteArray(
            R"({"name": "a", "profession": "p", "offers": [{"costs": [{"item": "x", "count": 0}], "result": {"item": "y"}}]})");
        QTest::newRow("same id") << QByteArray(R"({"name": "a", "profession": "p", "offers": [{"costs": [{"item": "x"}], "result": {"item": "y"}},
            {"costs": [{"item": "x", "count": 2}], "result": {"item": "y"}}]})");
    }

    void brokenTradeFilesAreRefused()
    {
        QFETCH(QByteArray, json);
        QVERIFY(!QcTradeParser::parseTradeSet(QJsonDocument::fromJson(json).object(), u"qtcraft"_s).has_value());
    }

    void tradingSettingsAreRead()
    {
        const QJsonObject good = parse(R"({"trading": {"levels": [0, 5, 6, 7, 100], "offersPerLevel": 3, "restocksPerDay": 1, "restockGap": 0.25}})");
        QcJsonFieldReader reader{good, u"test"_s, {}};
        const QcTradingSettings settings = QcTradeParser::readTradingSettings(reader);
        QVERIFY(reader.ok());
        QCOMPARE(settings.levels, (QList<int>{0, 5, 6, 7, 100}));
        QCOMPARE(std::tuple(settings.offersPerLevel, settings.restocksPerDay, settings.restockGap), std::tuple(3, 1, 0.25));

        for (const char *broken : {R"({"trading": {"levels": [0, 5, 5, 7, 100]}})", R"({"trading": {"levels": [1, 5, 6, 7, 100]}})",
                                   R"({"trading": {"levels": [0, 5, 6]}})"}) {
            const QJsonObject json = parse(broken);
            QcJsonFieldReader bad{json, u"test"_s, {}};
            QcTradeParser::readTradingSettings(bad);
            QVERIFY2(!bad.ok(), broken);
        }
    }

    void theOfferStateIsSavedWithTheVillager()
    {
        QcVillagerRecord villager = villagerOf(u"qtcraft:fisher"_s, 3);
        villager.experience = 77;
        villager.offers = {{u"qtcraft:string>qtcraft:emerald"_s, 16, 2}, {u"b"_s, 0, 0}};
        villager.lastRestock = 6123.5;
        villager.restocks = 2;
        QByteArray bytes;
        {
            QDataStream out(&bytes, QIODevice::WriteOnly);
            QcVillagerCodec::write(out, villager);
        }
        QDataStream in(bytes);
        QCOMPARE(QcVillagerCodec::read(in, QcVillagerCodec::version), std::optional(villager));

        // Damaged: a restock time beyond any world time or before it began.
        for (const double time : {1e300, -5.0, qInf()}) {
            QcVillagerRecord broken = villager;
            broken.lastRestock = time;
            QByteArray damaged;
            QDataStream out(&damaged, QIODevice::WriteOnly);
            QcVillagerCodec::write(out, broken);
            QDataStream back(damaged);
            QVERIFY(!QcVillagerCodec::read(back, QcVillagerCodec::version));
        }

        // Damaged: too many offers, negative uses.
        for (const auto &damage : {std::pair(QcVillagerRecord::maxOffers + 1, 0), std::pair(1, -1)}) {
            QcVillagerRecord broken = villager;
            broken.offers = QList<QcVillagerOffer>(damage.first, QcVillagerOffer{u"x"_s, damage.second, 0});
            QByteArray damaged;
            QDataStream out(&damaged, QIODevice::WriteOnly);
            QcVillagerCodec::write(out, broken);
            QDataStream back(damaged);
            QVERIFY(!QcVillagerCodec::read(back, QcVillagerCodec::version));
        }
    }

    void tradeMessagesRoundTrip()
    {
        QcMsg::TradeState state{12, u"profession.qtcraft.smith"_s, 2, 30, 10, 70, {}};
        state.offers.append({{{static_cast<QcItemId>(3), 4, 0}, {static_cast<QcItemId>(5), 1, 0}}, {static_cast<QcItemId>(9), 1, 0}, 2, 12});
        const QcResult<QcMessage> decoded = QcProtocol::decode(QcProtocol::encode(state), QcProtocol::Peer::Server);
        QVERIFY(decoded && std::get<QcMsg::TradeState>(*decoded) == state);

        const QcResult<QcMessage> trade = QcProtocol::decode(QcProtocol::encode(QcMsg::Trade{12, 1, true}), QcProtocol::Peer::Client);
        QVERIFY(trade && std::get<QcMsg::Trade>(*trade).repeat && std::get<QcMsg::Trade>(*trade).offer == 1);
        QVERIFY(QcProtocol::decode(QcProtocol::encode(QcMsg::CloseTrade{}), QcProtocol::Peer::Client));
        QVERIFY(QcProtocol::decode(QcProtocol::encode(QcMsg::CloseTrade{}), QcProtocol::Peer::Server));
        // Only the server sends the screen, only the client trades.
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(state), QcProtocol::Peer::Client));
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::Trade{}), QcProtocol::Peer::Server));

        // An offer without costs or with uses beyond its limit is refused.
        state.offers[0].uses = 13;
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(state), QcProtocol::Peer::Server));
        state.offers[0] = {{}, {static_cast<QcItemId>(9), 1, 0}, 0, 1};
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(state), QcProtocol::Peer::Server));
    }
};

QTEST_GUILESS_MAIN(TestTradeOffers)
#include "test_trade_offers.moc"

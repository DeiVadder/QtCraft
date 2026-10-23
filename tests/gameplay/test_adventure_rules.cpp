#include "common/content/definitions/QcBlockDefinition.h"
#include "common/content/definitions/QcItemDefinition.h"
#include "common/content/parser/QcStructureParser.h"
#include "common/gameplay/QcAdventureRules.h"

#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

using namespace Qt::StringLiterals;

// The adventure allow-lists (decision E-735): the one rule server and client decide with, the
// limits of the lists, world.json and the wire format.
class TestAdventureRules : public QObject
{
    Q_OBJECT

private:
    using Action = QcAdventureRules::Action;

    static QcItemDefinition itemNamed(const QString &name)
    {
        QcItemDefinition item;
        item.name = name;
        return item;
    }

    static QcBlockDefinition blockNamed(const QString &name)
    {
        QcBlockDefinition block;
        block.name = name;
        return block;
    }

private slots:
    void onlyAdventureIsLimited()
    {
        const QcAdventureRules none;
        const QcItemDefinition pickaxe = itemNamed(u"qtcraft:iron_pickaxe"_s);
        const QcBlockDefinition stone = blockNamed(u"qtcraft:stone"_s);
        for (const QcGameMode mode : {QcGameMode::Survival, QcGameMode::Creative}) {
            QVERIFY(none.permits(mode, Action::Break, &pickaxe, stone));
            QVERIFY(none.permits(mode, Action::PlaceOn, nullptr, stone));
        }
        QVERIFY(!none.permits(QcGameMode::Adventure, Action::Break, &pickaxe, stone));
        QVERIFY(!none.permits(QcGameMode::Adventure, Action::Break, nullptr, stone));
        QVERIFY(!none.permits(QcGameMode::Adventure, Action::PlaceOn, &pickaxe, stone));
        QVERIFY(QcGameModes::rules(QcGameMode::Adventure).buildByAllowList);
        QVERIFY(!QcGameModes::rules(QcGameMode::Adventure).mayFly);
        QVERIFY(!QcGameModes::rules(QcGameMode::Adventure).invulnerable);
        QCOMPARE(QcGameModes::inventorySet(QcGameMode::Adventure), QcGameModes::inventorySet(QcGameMode::Survival));
    }

    void listsArePerItemKindAndAction()
    {
        QcAdventureRules rules;
        QVERIFY(rules.allow(Action::Break, u"qtcraft:iron_pickaxe"_s, {u"qtcraft:stone"_s}));
        QVERIFY(rules.allow(Action::PlaceOn, u"qtcraft:stone"_s, {u"qtcraft:grass_block"_s}));
        const QcItemDefinition pickaxe = itemNamed(u"qtcraft:iron_pickaxe"_s);
        const QcItemDefinition shovel = itemNamed(u"qtcraft:iron_shovel"_s);
        const QcItemDefinition cobble = itemNamed(u"qtcraft:stone"_s);
        const QcBlockDefinition stone = blockNamed(u"qtcraft:stone"_s);
        const QcBlockDefinition grass = blockNamed(u"qtcraft:grass_block"_s);

        QVERIFY(rules.permits(QcGameMode::Adventure, Action::Break, &pickaxe, stone));
        QVERIFY(!rules.permits(QcGameMode::Adventure, Action::Break, &pickaxe, grass));
        QVERIFY(!rules.permits(QcGameMode::Adventure, Action::Break, &shovel, stone));
        QVERIFY(!rules.permits(QcGameMode::Adventure, Action::PlaceOn, &pickaxe, stone));
        QVERIFY(rules.permits(QcGameMode::Adventure, Action::PlaceOn, &cobble, grass));
        QVERIFY(!rules.permits(QcGameMode::Adventure, Action::Break, &cobble, grass));
    }

    void anyItemCoversTheHandToo()
    {
        QcAdventureRules rules;
        QVERIFY(rules.allow(Action::Break, QString::fromLatin1(QcAdventureRules::anyItem), {u"qtcraft:dirt"_s}));
        const QcItemDefinition stick = itemNamed(u"qtcraft:stick"_s);
        QVERIFY(rules.permits(QcGameMode::Adventure, Action::Break, nullptr, blockNamed(u"qtcraft:dirt"_s)));
        QVERIFY(rules.permits(QcGameMode::Adventure, Action::Break, &stick, blockNamed(u"qtcraft:dirt"_s)));
        QVERIFY(!rules.permits(QcGameMode::Adventure, Action::Break, &stick, blockNamed(u"qtcraft:sand"_s)));
    }

    void bottlesFillEverywhere()
    {
        const QcAdventureRules none;
        QcItemDefinition bottle = itemNamed(u"qtcraft:glass_bottle"_s);
        bottle.bottles = true;
        QVERIFY(none.permits(QcGameMode::Adventure, Action::PlaceOn, &bottle, blockNamed(u"qtcraft:water"_s)));
        QVERIFY(!none.permits(QcGameMode::Adventure, Action::Break, &bottle, blockNamed(u"qtcraft:water"_s)));
    }

    void removingAndLimits()
    {
        QcAdventureRules rules;
        QVERIFY(rules.allow(Action::Break, u"*"_s, {u"b"_s, u"a"_s, u"b"_s}));
        QCOMPARE(rules.blocks(Action::Break, u"*"_s), (QStringList{u"a"_s, u"b"_s}));
        QVERIFY(rules.remove(Action::Break, u"*"_s, {u"a"_s}));
        QVERIFY(!rules.remove(Action::Break, u"*"_s, {u"a"_s}));
        QVERIFY(!rules.remove(Action::PlaceOn, u"*"_s, {}));
        QVERIFY(rules.remove(Action::Break, u"*"_s, {}));
        QVERIFY(rules.isEmpty());

        // Too many blocks for one item, too many items, too long a name: nothing changes.
        QStringList many;
        for (int i = 0; i <= QcAdventureRules::maxBlocks; ++i) {
            many.append(u"block%1"_s.arg(i));
        }
        QVERIFY(!rules.allow(Action::Break, u"*"_s, many));
        QVERIFY(!rules.allow(Action::Break, QString(QcAdventureRules::maxNameLength + 1, u'x'), {u"a"_s}));
        for (int i = 0; i < QcAdventureRules::maxItems; ++i) {
            QVERIFY(rules.allow(Action::PlaceOn, u"item%1"_s.arg(i), {u"a"_s}));
        }
        QVERIFY(!rules.allow(Action::PlaceOn, u"one_more"_s, {u"a"_s}));
        QVERIFY(rules.allow(Action::PlaceOn, u"item0"_s, {u"b"_s}));
    }

    void allNamesTogetherStayWithinTheCap()
    {
        // The lists of the largest legal set fit a protocol frame many times over.
        static_assert(static_cast<qint64>(QcAdventureRules::maxNames) * (4 + 2 * static_cast<qint64>(QcAdventureRules::maxNameLength))
                      < qint64{2} * 1024 * 1024);

        QcAdventureRules rules;
        QStringList blocks;
        for (int i = 0; i < QcAdventureRules::maxBlocks - 1; ++i) {
            blocks.append(u"block%1"_s.arg(i));
        }
        // 16 items with 255 blocks each: 16 x 256 = 4096 names, the cap.
        for (int i = 0; i < 16; ++i) {
            QVERIFY(rules.allow(i % 2 == 0 ? Action::Break : Action::PlaceOn, u"item%1"_s.arg(i), blocks));
        }
        QCOMPARE(rules.nameCount(), QcAdventureRules::maxNames);
        QVERIFY(!rules.allow(Action::Break, u"one_more"_s, {u"a"_s}));
        QVERIFY(!rules.allow(Action::Break, u"item0"_s, {u"another"_s}));
        // Blocks already listed add nothing.
        QVERIFY(rules.allow(Action::Break, u"item0"_s, {u"block0"_s}));

        // The same set from world.json or the wire: the part beyond the cap is refused.
        QJsonObject json = rules.toJson();
        // The last key read (canPlaceOn, sorted) is the one beyond the cap.
        QJsonObject placing = json.value(u"canPlaceOn"_s).toObject();
        placing.insert(u"zzz"_s, QJsonArray{u"a"_s});
        json.insert(u"canPlaceOn"_s, placing);
        const QcAdventureRules::Read read = QcAdventureRules::fromJson(json);
        QCOMPARE(read.rules.nameCount(), QcAdventureRules::maxNames);
        QCOMPARE(read.problems.size(), 1);

        QByteArray bytes;
        QDataStream out(&bytes, QIODevice::WriteOnly);
        rules.write(out);
        // One more item announced for canBreak: the cap refuses the whole message.
        QByteArray larger;
        QDataStream more(&larger, QIODevice::WriteOnly);
        for (const Action action : {Action::Break, Action::PlaceOn}) {
            const QStringList items = rules.items(action);
            const bool extra = action == Action::Break;
            more << static_cast<quint32>(items.size() + (extra ? 1 : 0));
            for (const QString &item : items) {
                const QStringList listed = rules.blocks(action, item);
                more << item << static_cast<quint32>(listed.size());
                for (const QString &block : listed) {
                    more << block;
                }
            }
            if (extra) {
                more << u"zzz"_s << quint32{1} << u"a"_s;
            }
        }
        QDataStream in(larger);
        QVERIFY(!QcAdventureRules::read(in));
        QDataStream whole(bytes);
        QVERIFY(QcAdventureRules::read(whole));
    }

    void structuresHaveListsOnlyOnShow()
    {
        const auto parse = [](const char *text) {
            return QcStructureParser::parseStructure(QJsonDocument::fromJson(text).object(), u"qtcraft"_s);
        };
        const QcResult<QcStructureDefinition> shown = parse(
            "{\"name\": \"a\", \"showcase\": {\"order\": 1}, \"palette\": {\"#\": \"stone\"}, \"layers\": [[\"#\"]], \"adventure\": {\"canBreak\": {\"stone_pickaxe\": [\"mossy_cobblestone\"], \"*\": [\"glass\"]}}}");
        QVERIFY2(shown, qPrintable(shown ? QString() : shown.error()));
        QCOMPARE(shown->adventure.blocks(Action::Break, u"qtcraft:stone_pickaxe"_s), QStringList{u"qtcraft:mossy_cobblestone"_s});
        QCOMPARE(shown->adventure.blocks(Action::Break, u"*"_s), QStringList{u"qtcraft:glass"_s});

        QVERIFY(!parse("{\"name\": \"b\", \"palette\": {\"#\": \"stone\"}, \"layers\": [[\"#\"]], \"adventure\": {\"canBreak\": {\"*\": [\"glass\"]}}}"));
        QVERIFY(!parse("{\"name\": \"c\", \"showcase\": {\"order\": 1}, \"palette\": {\"#\": \"stone\"}, \"layers\": [[\"#\"]], \"adventure\": 3}"));
        QVERIFY(!parse("{\"name\": \"d\", \"showcase\": {\"order\": 1}, \"palette\": {\"#\": \"stone\"}, \"layers\": [[\"#\"]], \"adventure\": {\"canBreak\": {\"*\": \"glass\"}}}"));
    }

    void actionNames()
    {
        QCOMPARE(QcAdventureRules::parseAction(u"canBreak"), std::optional(Action::Break));
        QCOMPARE(QcAdventureRules::parseAction(u"CANPLACEON"), std::optional(Action::PlaceOn));
        QVERIFY(!QcAdventureRules::parseAction(u"place"));
        QCOMPARE(QcGameModes::parse(u"adventure"), std::optional(QcGameMode::Adventure));
        QCOMPARE(QcGameModes::parse(u"A"), std::optional(QcGameMode::Adventure));
        QCOMPARE(QcGameModes::parse(u"2"), std::optional(QcGameMode::Adventure));
        QCOMPARE(QcGameModes::name(QcGameMode::Adventure), "adventure"_L1);
        QCOMPARE(QcGameModes::fromValue(2), std::optional(QcGameMode::Adventure));
    }

    void jsonKeepsTheListsAndSkipsWhatIsDamaged()
    {
        QcAdventureRules rules;
        QVERIFY(rules.allow(Action::Break, u"qtcraft:iron_pickaxe"_s, {u"qtcraft:stone"_s, u"mod:gone"_s}));
        QVERIFY(rules.allow(Action::PlaceOn, u"*"_s, {u"qtcraft:oak_planks"_s}));
        const QcAdventureRules::Read back = QcAdventureRules::fromJson(rules.toJson());
        QVERIFY(back.rules == rules);
        QVERIFY(back.problems.isEmpty());
        QVERIFY(QcAdventureRules{}.toJson().isEmpty());

        // Every dropped key is named with its reason; the rest is kept.
        const QJsonObject damaged =
            QJsonDocument::fromJson("{\"canBreak\": {\"*\": [\"qtcraft:dirt\"], \"y\": [\"a\", 5], \"x\": \"notalist\"}, \"canPlaceOn\": 3}").object();
        const QcAdventureRules::Read read = QcAdventureRules::fromJson(damaged);
        QCOMPARE(read.rules.items(Action::Break), QStringList{u"*"_s});
        QCOMPARE(read.rules.blocks(Action::Break, u"*"_s), QStringList{u"qtcraft:dirt"_s});
        QVERIFY(read.rules.items(Action::PlaceOn).isEmpty());
        QCOMPARE(read.problems.size(), 3);
        QVERIFY(read.problems.join(u'|').contains(u"'canPlaceOn' is no object"_s));
        QVERIFY(read.problems.join(u'|').contains(u"'x' of 'canBreak'"_s));
        QVERIFY(read.problems.join(u'|').contains(u"'y' of 'canBreak'"_s));
    }

    void streamRoundTrip()
    {
        QcAdventureRules rules;
        QVERIFY(rules.allow(Action::Break, u"qtcraft:iron_pickaxe"_s, {u"qtcraft:stone"_s}));
        QByteArray bytes;
        QDataStream out(&bytes, QIODevice::WriteOnly);
        rules.write(out);
        QDataStream in(bytes);
        const std::optional<QcAdventureRules> read = QcAdventureRules::read(in);
        QVERIFY(read);
        QVERIFY(*read == rules);

        // Cut short: refused.
        QDataStream cut(bytes.left(bytes.size() - 3));
        QVERIFY(!QcAdventureRules::read(cut));
    }
};

QTEST_MAIN(TestAdventureRules)
#include "test_adventure_rules.moc"

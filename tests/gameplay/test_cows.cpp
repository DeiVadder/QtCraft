#include "clientcore/session/QcClientLauncher.h"
#include "common/content/registry/QcContentSerializer.h"
#include "QcServerTestFixture.h"
#include "server/gameplay/QcServerEffects.h"

using namespace Qt::StringLiterals;

// Cows and milk (decision E-500): milking with an empty bucket, drinking milk clears the status
// effects and leaves the bucket, breeding with wheat, their drops. Ticks are stepped by hand.
class TestCows : public QcServerTestFixture
{
    Q_OBJECT

private:
    static inline const QString poison = u"qtcraft:poison"_s;

    int cowType() const { return *m_content->mobIndex(u"qtcraft:cow"_s); }
    const QcMobDefinition &cow() const { return m_content->mobs()[cowType()]; }
    QcCreature *mob(quint32 id) { return m_server->entities().creatures().find(id); }
    QcServerClient &player() { return *m_server->entities().clients().front(); }

    quint32 spawn(int dx, int dz)
    {
        const QcVec3 at{m_feet.x + dx + 0.5, double(m_feet.y), m_feet.z + dz + 0.5};
        return m_server->entities().creatures().spawn(cowType(), at, m_server->entities().newId(), false, true);
    }

    qsizetype cows() const { return std::ranges::count(m_server->entities().creatures().mobs(), cowType(), &QcCreature::type); }

    void hold(const char *name)
    {
        const int slot = slotOf(item(name));
        QVERIFY2(slot >= 0 && slot < 9, name);
        send(QcMsg::SelectHotbarSlot{slot});
    }

    void joinSurvival()
    {
        join();
        chat(u"/gamemode survival"_s);
        ticks(1);
    }

private slots:
    void cowsComeFromData()
    {
        QCOMPARE(cow().milkWith, u"qtcraft:bucket"_s);
        QCOMPARE(cow().milkItem, u"qtcraft:milk_bucket"_s);
        QVERIFY(cow().breeding.food.contains(u"qtcraft:wheat"_s));
        QVERIFY(std::ranges::find(cow().drops, u"qtcraft:leather"_s, &QcItemDrop::item) != cow().drops.end());
        QVERIFY(std::ranges::find(cow().drops, u"qtcraft:raw_beef"_s, &QcItemDrop::item) != cow().drops.end());
        QVERIFY(std::ranges::any_of(m_content->smeltingRecipes(), [](const QcSmeltingRecipe &recipe) {
            return recipe.inputItem == u"qtcraft:raw_beef"_s && recipe.resultItem == u"qtcraft:cooked_beef"_s;
        }));
        const QcItemDefinition &milk = m_content->item(item("qtcraft:milk_bucket"));
        QVERIFY(milk.drink && milk.drink->clearsEffects);
        QCOMPARE(milk.emptiesTo, u"qtcraft:bucket"_s);
        QCOMPARE(milk.maxStack, 1);
    }

    void milkingFillsOneBucketOfTheStack()
    {
        joinSurvival();
        const quint32 grown = spawn(2, 0);
        chat(u"/give bucket 3"_s);
        hold("qtcraft:bucket");
        send(QcMsg::UseEntity{grown});
        QCOMPARE(countOf(item("qtcraft:milk_bucket")), 1);
        QCOMPARE(countOf(item("qtcraft:bucket")), 2);
        // The cow stays.
        QVERIFY(mob(grown));

        // A calf gives no milk.
        const quint32 calf = spawn(-2, 0);
        mob(calf)->growUp = 1000.0;
        hold("qtcraft:bucket");
        send(QcMsg::UseEntity{calf});
        QCOMPARE(countOf(item("qtcraft:milk_bucket")), 1);
        QCOMPARE(countOf(item("qtcraft:bucket")), 2);
    }

    void drinkingMilkClearsPoisonAndLeavesTheBucket()
    {
        joinSurvival();
        QVERIFY(QcServerEffects::givePlayer(*m_content, player(), {poison, 1, 600}));
        ticks(1);
        QVERIFY(!m_client->effects.isEmpty());

        chat(u"/give milk_bucket 1"_s);
        hold("qtcraft:milk_bucket");
        send(QcMsg::ConsumeItem{});
        QVERIFY(player().effects.isEmpty());
        QVERIFY(m_client->effects.isEmpty());
        QCOMPARE(countOf(item("qtcraft:milk_bucket")), 0);
        QCOMPARE(countOf(item("qtcraft:bucket")), 1);

        // Without effects the milk is drunk all the same.
        chat(u"/give milk_bucket 1"_s);
        hold("qtcraft:milk_bucket");
        send(QcMsg::ConsumeItem{});
        QCOMPARE(countOf(item("qtcraft:milk_bucket")), 0);
        QCOMPARE(countOf(item("qtcraft:bucket")), 2);
    }

    void creativePlayersKeepTheirMilk()
    {
        join();
        chat(u"/gamemode creative"_s);
        QVERIFY(QcServerEffects::givePlayer(*m_content, player(), {poison, 1, 600}));
        chat(u"/give milk_bucket 1"_s);
        hold("qtcraft:milk_bucket");
        send(QcMsg::ConsumeItem{});
        QVERIFY(player().effects.isEmpty());
        QCOMPARE(countOf(item("qtcraft:milk_bucket")), 1);
    }

    void aCalfTakesTheUseWithoutMilk()
    {
        joinSurvival();
        const quint32 calf = spawn(2, 0);
        mob(calf)->growUp = 1000.0;
        chat(u"/give bucket 1"_s);
        hold("qtcraft:bucket");
        // The bucket goes to the calf, which gives nothing: no milk, the bucket stays empty in the hand.
        send(QcMsg::UseEntity{calf});
        QCOMPARE(countOf(item("qtcraft:milk_bucket")), 0);
        QCOMPARE(countOf(item("qtcraft:bucket")), 1);
        QVERIFY(mob(calf));
        QCOMPARE(mob(calf)->love, 0.0);
    }

    void creativeMilkingKeepsTheBucket()
    {
        join();
        chat(u"/gamemode creative"_s);
        const quint32 cow = spawn(2, 0);
        chat(u"/give bucket 1"_s);
        hold("qtcraft:bucket");
        send(QcMsg::UseEntity{cow});
        // As in the reference: the empty bucket stays in the hand and the milk is added.
        QCOMPARE(countOf(item("qtcraft:bucket")), 1);
        QCOMPARE(countOf(item("qtcraft:milk_bucket")), 1);
        QCOMPARE(m_client->stacks[player().inventory.selectedHotbarSlot()].item, item("qtcraft:bucket"));
    }

    void theClientUsesBucketsRodsAndDrinks()
    {
        // Through the content the client gets from the server.
        QByteArray bytes;
        {
            QDataStream out(&bytes, QIODevice::WriteOnly);
            QcContentSerializer::write(*m_content, out);
        }
        QDataStream in(bytes);
        const QcResult<QcContentRegistry> client = QcContentSerializer::read(in);
        QVERIFY(client.has_value());
        const auto itemOf = [&client](const QString &name) -> const QcItemDefinition & { return client->item(*client->itemId(name)); };
        const QcMobDefinition &cow = client->mobs()[*client->mobIndex(u"qtcraft:cow"_s)];
        QCOMPARE(cow.milkWith, u"qtcraft:bucket"_s);
        const QcItemDefinition &milk = itemOf(u"qtcraft:milk_bucket"_s);
        QVERIFY(milk.drink.has_value());
        QCOMPARE(milk.drink->clearsEffects, m_content->item(item("qtcraft:milk_bucket")).drink->clearsEffects);
        const QcItemDefinition &rod = itemOf(u"qtcraft:fishing_rod"_s);
        QVERIFY(rod.fishing.has_value());
        QVERIFY(!itemOf(u"qtcraft:bucket"_s).fishing && !itemOf(u"qtcraft:bucket"_s).drink);

        // A bucket goes to a cow; milk is drunk, also with a block aimed at; a rod casts (ThrowItem).
        QVERIFY(QcClientLauncher::usesOnCreature(cow, itemOf(u"qtcraft:bucket"_s)));
        QVERIFY(!QcClientLauncher::usesOnCreature(cow, itemOf(u"qtcraft:stick"_s)));
        QVERIFY(QcClientLauncher::consumes(milk, true));
        QVERIFY(!QcClientLauncher::consumes(itemOf(u"qtcraft:bucket"_s), false));
        QVERIFY(QcClientLauncher::uses(rod));
        QcInventory inventory;
        inventory.setSlot(0, {*client->itemId(u"qtcraft:fishing_rod"_s), 1});
        QcClientLauncher launcher;
        const std::optional<QcMessage> cast = launcher.use(*client->itemId(u"qtcraft:fishing_rod"_s), rod, inventory, *client, false);
        QVERIFY(cast && std::holds_alternative<QcMsg::ThrowItem>(*cast));
    }

    void cowsBreedWithWheat()
    {
        join();
        const quint32 first = spawn(2, 0);
        const quint32 second = spawn(-2, 1);
        chat(u"/give wheat 2"_s);
        hold("qtcraft:wheat");
        send(QcMsg::UseEntity{first});
        send(QcMsg::UseEntity{second});
        QVERIFY(mob(first)->love > 0.0 && mob(second)->love > 0.0);
        for (int i = 0; i < 100 && cows() < 3; ++i) {
            ticks(1);
        }
        QCOMPARE(cows(), 3);
        const auto young = std::ranges::find_if(m_server->entities().creatures().mobs(), &QcCreature::isYoung);
        QVERIFY(young != m_server->entities().creatures().mobs().end() && young->type == cowType());
    }
};

QTEST_MAIN(TestCows)
#include "test_cows.moc"

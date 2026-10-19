#include "common/physics/QcPhysics.h"
#include "QcTestMachines.h"
#include "QcTestWorld.h"

#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr double dt = QcPhysics::tickDuration;

int ticksFor(double seconds)
{
    return int(std::ceil(seconds / dt));
}

// Own small content with round numbers: one ore takes 1 s, coal burns 3.5 s, a twig 0.5 s.
std::shared_ptr<const QcContentRegistry> machineContent()
{
    auto content = std::make_shared<QcContentRegistry>();
    const auto block = [&content](const QString &name, const QcMachineDefinition &machine) {
        QcBlockDefinition definition;
        definition.name = name;
        definition.machine = machine;
        definition.faceTextures.fill(name);
        if (!content->registerBlock(definition)) {
            // Builds content for a member initialiser, outside any test case; no case can run without it.
            qFatal("cannot register block");
        }
    };

    block(u"t:furnace"_s, {u"smelting"_s, 1.0, {}});
    block(u"t:fast_furnace"_s, {u"smelting"_s, 2.0, {}});
    block(u"t:crusher"_s, {u"crushing"_s, 1.0, {}});
    block(u"t:blast_furnace"_s, {u"smelting"_s, 100.0, {}});
    block(u"t:stone"_s, {});

    const auto item = [&content](const QString &name, double fuel, int maxStack, const QString &emptiesTo = {}) {
        QcItemDefinition definition;
        definition.name = name;
        definition.fuelSeconds = fuel;
        definition.maxStack = maxStack;
        definition.emptiesTo = emptiesTo;
        if (!content->registerItem(definition)) {
            // Builds content for a member initialiser, outside any test case; no case can run without it.
            qFatal("cannot register item");
        }
    };

    item(u"t:ore"_s, 0.0, 64);
    item(u"t:ingot"_s, 0.0, 4);
    item(u"t:gravel"_s, 0.0, 64);
    item(u"t:coal"_s, 3.5, 64);
    item(u"t:twig"_s, 0.5, 64);
    item(u"t:bucket"_s, 0.0, 16);
    item(u"t:lava_bucket"_s, 2.0, 1, u"t:bucket"_s);
    item(u"t:oil_can"_s, 2.0, 4, u"t:bucket"_s);

    content->registerSmeltingRecipe({u"t:ingot"_s, u"smelting"_s, u"t:ore"_s, u"t:ingot"_s, 1, 1.0});
    content->registerSmeltingRecipe({u"t:gravel"_s, u"crushing"_s, u"t:ore"_s, u"t:gravel"_s, 2, 1.0});
    return content;
}

using Machines = QcTestMachines;

} // namespace

// Machine blocks on the server: fuel, smelting progress, limits and loaded chunks.
class TestMachines : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = machineContent();
    const QcBlockPos m_pos{2, 64, 3};

    QcItemId item(const char *name) const { return *m_content->itemId(QString::fromLatin1(name)); }
    QcBlockId block(const char *name) const { return *m_content->blockId(QString::fromLatin1(name)); }

    QcTestWorld worldWith(const char *machine) const
    {
        QcTestWorld world(*m_content);
        world.set(m_pos, block(machine));
        return world;
    }

    // A machine at m_pos with the given input and fuel.
    QcBlockEntities::Machine &setUp(Machines &machines, QcItemStack input, QcItemStack fuel, QcItemStack output = {})
    {
        if (!machines.entities.ensure(m_pos, block("t:furnace"))) {
            // Callers hold on to the returned machine, and nothing could stand in for it; only broken
            // test content (a furnace that is no machine) gets here.
            qFatal("cannot add machine");
        }
        QcBlockEntities::Machine &machine = *machines.entities.machine(m_pos);
        machine.stacks = {input, fuel, output};
        return machine;
    }

    static void run(Machines &machines, const QcTestWorld &world, int ticks)
    {
        for (int i = 0; i < ticks; ++i) {
            machines.rules.tick(machines.entities, world, dt);
        }
    }

private slots:
    void fuelBurnsAndItemsSmelt()
    {
        const QcTestWorld world = worldWith("t:furnace");
        Machines machines(m_content);
        QcBlockEntities::Machine &machine = setUp(machines, {item("t:ore"), 5, 0}, {item("t:coal"), 2, 0});

        // The first tick lights one coal.
        run(machines, world, 1);
        QCOMPARE(machine.stacks[QcMachineRules::fuelSlot].count, 1);
        QCOMPARE(machine.burnSeconds, 3.5);
        run(machines, world, ticksFor(1.0));
        QCOMPARE(machine.stacks[QcMachineRules::outputSlot], (QcItemStack{item("t:ingot"), 1, 0}));
        QCOMPARE(machine.stacks[QcMachineRules::inputSlot].count, 4);

        // 3.5 s of coal make three ingots; the fourth needs the second coal.
        run(machines, world, ticksFor(2.0) + 1);
        QCOMPARE(machine.stacks[QcMachineRules::outputSlot].count, 3);
        QCOMPARE(machine.stacks[QcMachineRules::fuelSlot].count, 1);
        QVERIFY(machines.burnFraction(machine) > 0.0 && machines.burnFraction(machine) < 0.2);
        run(machines, world, ticksFor(0.6));
        QCOMPARE(machine.stacks[QcMachineRules::fuelSlot].count, 0);
        QVERIFY(machines.burnFraction(machine) > 0.8);
    }

    void fuelIsChangedWhenItRunsOut()
    {
        const QcTestWorld world = worldWith("t:furnace");
        Machines machines(m_content);
        QcBlockEntities::Machine &machine = setUp(machines, {item("t:ore"), 2, 0}, {item("t:twig"), 3, 0});

        // A twig lasts 10 ticks: after 11 the second one burns, and two of them make one ingot.
        run(machines, world, 11);
        QCOMPARE(machine.stacks[QcMachineRules::fuelSlot].count, 1);
        QVERIFY(machine.stacks[QcMachineRules::outputSlot].isEmpty());
        run(machines, world, 11);
        QCOMPARE(machine.stacks[QcMachineRules::outputSlot].count, 1);

        // The third twig does half of the second ingot; then the progress falls back twice as fast.
        run(machines, world, 9);
        QCOMPARE(machine.stacks[QcMachineRules::fuelSlot].count, 0);
        QVERIFY(machines.burnFraction(machine) < 0.05);
        const double halfDone = machines.progressFraction(machine, block("t:furnace"));
        QVERIFY2(halfDone >= 0.35 && halfDone <= 0.55, qPrintable(QString::number(halfDone)));
        run(machines, world, 6);
        QCOMPARE(machine.burnRemaining, 0.0);
        QCOMPARE(machines.progressFraction(machine, block("t:furnace")), 0.0);
        QCOMPARE(machine.stacks[QcMachineRules::outputSlot].count, 1);
        QCOMPARE(machine.stacks[QcMachineRules::inputSlot].count, 1);
    }

    void fuelInAContainerLeavesTheContainer()
    {
        const QcTestWorld world = worldWith("t:furnace");
        Machines machines(m_content);
        QcBlockEntities::Machine &machine = setUp(machines, {item("t:ore"), 5, 0}, {item("t:lava_bucket"), 1, 0});
        run(machines, world, 1);
        QCOMPARE(machine.burnSeconds, 2.0);
        QCOMPARE(machine.stacks[QcMachineRules::fuelSlot], (QcItemStack{item("t:bucket"), 1, 0}));

        // The empty bucket is no fuel: after the lava two ingots are done and the fire goes out.
        run(machines, world, ticksFor(2.0) + 2);
        QCOMPARE(machine.stacks[QcMachineRules::outputSlot].count, 2);
        QCOMPARE(machine.burnRemaining, 0.0);
        QCOMPARE(machine.stacks[QcMachineRules::fuelSlot], (QcItemStack{item("t:bucket"), 1, 0}));

        // Stacked containers have no room for the empty one, so they do not burn.
        Machines other(m_content);
        QcBlockEntities::Machine &stacked = setUp(other, {item("t:ore"), 5, 0}, {item("t:oil_can"), 2, 0});
        run(other, world, 3);
        QCOMPARE(stacked.burnRemaining, 0.0);
        QCOMPARE(stacked.stacks[QcMachineRules::fuelSlot], (QcItemStack{item("t:oil_can"), 2, 0}));
        QVERIFY(stacked.stacks[QcMachineRules::outputSlot].isEmpty());
    }

    void aFullOutputStopsWithoutBurningFuel()
    {
        const QcTestWorld world = worldWith("t:furnace");
        Machines machines(m_content);
        // Ingots stack to four.
        QcBlockEntities::Machine &machine = setUp(machines, {item("t:ore"), 1, 0}, {item("t:coal"), 1, 0}, {item("t:ingot"), 4, 0});
        run(machines, world, ticksFor(2.0));
        QCOMPARE(machine.stacks[QcMachineRules::fuelSlot].count, 1);
        QCOMPARE(machine.stacks[QcMachineRules::inputSlot].count, 1);
        QCOMPARE(machine.burnRemaining, 0.0);

        // Another item in the output blocks as well.
        machine.stacks[QcMachineRules::outputSlot] = {item("t:gravel"), 1, 0};
        run(machines, world, ticksFor(2.0));
        QCOMPARE(machine.stacks[QcMachineRules::fuelSlot].count, 1);
        machine.stacks[QcMachineRules::outputSlot] = {item("t:ingot"), 3, 0};
        run(machines, world, ticksFor(1.0) + 1);
        QCOMPARE(machine.stacks[QcMachineRules::outputSlot].count, 4);
        QVERIFY(machine.stacks[QcMachineRules::inputSlot].isEmpty());
    }

    void withoutARecipeNothingBurns()
    {
        const QcTestWorld world = worldWith("t:furnace");
        Machines machines(m_content);
        QcBlockEntities::Machine &machine = setUp(machines, {item("t:gravel"), 3, 0}, {item("t:coal"), 2, 0});
        run(machines, world, ticksFor(3.0));
        QCOMPARE(machine.stacks[QcMachineRules::fuelSlot].count, 2);
        QCOMPARE(machine.stacks[QcMachineRules::inputSlot].count, 3);
        QVERIFY(machine.stacks[QcMachineRules::outputSlot].isEmpty());
    }

    void categoriesAndSpeedComeFromTheBlock()
    {
        // The crusher takes the crushing recipe of the same ore: two gravel.
        {
            const QcTestWorld world = worldWith("t:crusher");
            Machines machines(m_content);
            QcBlockEntities::Machine &machine = setUp(machines, {item("t:ore"), 1, 0}, {item("t:coal"), 1, 0});
            run(machines, world, ticksFor(1.0) + 1);
            QCOMPARE(machine.stacks[QcMachineRules::outputSlot], (QcItemStack{item("t:gravel"), 2, 0}));
        }

        // Speed 2 halves the time per item and burns the coal twice as fast.
        const QcTestWorld world = worldWith("t:fast_furnace");
        Machines machines(m_content);
        QcBlockEntities::Machine &machine = setUp(machines, {item("t:ore"), 10, 0}, {item("t:coal"), 1, 0});
        run(machines, world, ticksFor(0.5) + 1);
        QCOMPARE(machine.stacks[QcMachineRules::outputSlot].count, 1);
        run(machines, world, ticksFor(1.5) + 1);
        QCOMPARE(machine.stacks[QcMachineRules::outputSlot].count, 3);
        QCOMPARE(machine.burnRemaining, 0.0);
        QCOMPARE(machine.stacks[QcMachineRules::outputSlot].count, 3);
    }

    void fastMachinesFinishSeveralItemsPerTick()
    {
        // Speed 100 does 5 s of work in one tick: five 1 s items, but the output holds only 4.
        const QcTestWorld world = worldWith("t:blast_furnace");
        Machines machines(m_content);
        QcBlockEntities::Machine &machine = setUp(machines, {item("t:ore"), 10, 0}, {item("t:coal"), 1, 0});
        run(machines, world, 1);
        QCOMPARE(machine.stacks[QcMachineRules::outputSlot], (QcItemStack{item("t:ingot"), 4, 0}));
        QCOMPARE(machine.stacks[QcMachineRules::inputSlot].count, 6);
        QCOMPARE(machine.progress, 0.0);
    }

    void unloadedMachinesDoNotTick()
    {
        QcTestWorld world = worldWith("t:furnace");
        world.loadedRadius = 1;
        Machines machines(m_content);
        QcBlockEntities::Machine &machine = setUp(machines, {item("t:ore"), 1, 0}, {item("t:coal"), 1, 0});
        run(machines, world, ticksFor(3.0));
        QCOMPARE(machine.stacks[QcMachineRules::fuelSlot].count, 1);
        QCOMPARE(machine.progress, 0.0);
        QCOMPARE(machines.count(), 1);
        world.loadedRadius = 1000;
        run(machines, world, ticksFor(1.0) + 1);
        QCOMPARE(machine.stacks[QcMachineRules::outputSlot].count, 1);
    }

    void aMachineWhoseBlockGoesSpillsItsContents()
    {
        Machines machines(m_content);
        setUp(machines, {item("t:ore"), 5, 0}, {item("t:coal"), 2, 0}, {item("t:ingot"), 1, 0});
        // Another machine keeps everything; a block that is no machine takes the entry away.
        machines.entities.placed(m_pos, block("t:furnace"), block("t:crusher"));
        QVERIFY(machines.entities.takeSpills().isEmpty());
        QCOMPARE(machines.count(), 1);
        machines.entities.placed(m_pos, block("t:crusher"), block("t:stone"));
        const QList<QcBlockEntities::Spill> spills = machines.entities.takeSpills();
        QCOMPARE(spills.size(), 1);
        QCOMPARE(spills.first().pos, m_pos);
        const QList<QcItemStack> expected{{item("t:ore"), 5, 0}, {item("t:coal"), 2, 0}, {item("t:ingot"), 1, 0}};
        QCOMPARE(spills.first().stacks, expected);
        QCOMPARE(machines.count(), 0);
        QVERIFY(!machines.entities.machine(m_pos));
    }

    void theNumberOfMachinesIsLimited()
    {
        Machines machines(m_content);
        const QcBlockId furnace = block("t:furnace");
        const qsizetype limit = machines.entities.limits().machines;
        for (int i = 0; i < limit; ++i) {
            QVERIFY(machines.entities.ensure({i, 64, 0}, furnace));
        }
        QVERIFY(machines.entities.isMachineTableFull());
        QVERIFY(!machines.entities.ensure({-1, 64, 0}, furnace));

        // An existing machine is still found; one from a save comes back beyond the limit (only
        // placing is refused).
        QVERIFY(machines.entities.ensure({5, 64, 0}, furnace));
        QcBlockEntities::Machine extra;
        extra.pos = {-5, 64, -5};
        QVERIFY(machines.entities.putMachine(extra));
        QCOMPARE(machines.count(), limit + 1);
        machines.entities.placed({5, 64, 0}, furnace, block("t:stone"));
        machines.entities.placed({6, 64, 0}, furnace, block("t:stone"));
        QVERIFY(machines.entities.ensure({-1, 64, 0}, furnace));
    }
};

QTEST_GUILESS_MAIN(TestMachines)
#include "test_machines.moc"

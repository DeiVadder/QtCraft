#include "QcServerTestFixture.h"
#include "QcShowcaseGenerator.h"
#include "QcWorldGenerators.h"

#include <QTemporaryDir>

using namespace Qt::StringLiterals;

// The Signal Dust station of the showcase world (decision E-340): its demos start by themselves
// once their chunks are loaded around a player, and each shows what it is about after some ticks:
// lit lamps, an opened door, dust shapes, a burnt-out clock, a waiting and an updated BUD piston, a
// spat block, a closed and an open piston door, a pushed chest with its contents and armour stand,
// a long wire.
class TestSignalShowcase : public QcServerTestFixture
{
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    QcShowcasePlan m_plan;

    QcShowcaseAreaInfo area(const QString &demo) const
    {
        for (const QcShowcaseAreaInfo &candidate : m_plan.areas) {
            if (candidate.name == demo) {
                return candidate;
            }
        }
        qFatal("no demo %s", qPrintable(demo));
    }

    QcBlockPos origin(const QString &demo) const { return area(demo).origin; }

    QString block(const QString &demo, int x, int y, int z) const
    {
        const std::optional<QcBlockId> id = m_server->world().blockAt(origin(demo).offset(x, y, z));
        return id ? m_content->block(*id).name.section(u':', 1) : u"unloaded"_s;
    }

    QString property(const QString &demo, int x, int y, int z, const QString &name) const
    {
        const std::optional<QcBlockId> id = m_server->world().blockAt(origin(demo).offset(x, y, z));
        return id ? m_content->property(*id, name) : u"unloaded"_s;
    }

    // Takes the player to the middle of a demo; its chunks load around him and its circuits start.
    void visit(const QString &demo)
    {
        const QcShowcaseAreaInfo info = area(demo);
        const QcBlockPos middle = info.origin.offset(info.sizeX / 2, 1, info.sizeZ / 2);
        chat(u"/tp %1 %2 %3"_s.arg(middle.x).arg(middle.y + 6).arg(middle.z));
        for (int dx = -2; dx <= 2; ++dx) {
            for (int dz = -2; dz <= 2; ++dz) {
                m_server->world().loadChunk({middle.chunk().x + dx, middle.chunk().z + dz});
            }
        }
        idle(60);
    }

private slots:
    void initTestCase()
    {
        // A world directory, so the showcase's setup runs; the player sees a whole demo.
        QVERIFY(m_directory.isValid());
        QcServerConfig config;
        config.generator = u"showcase"_s;
        config.worldDirectory = m_directory.path();
        config.autosaveSeconds = 0;
        m_viewDistance = 3;
        join(config, QcWorldGenerators::create(u"showcase"_s, m_content, 0));
        m_plan = QcShowcaseGenerator(m_content).plan();
    }

    void componentsShowTheirPower()
    {
        const QString demo = u"demo-signal_components"_s;
        visit(demo);
        QCOMPARE(block(demo, 2, 1, 1), u"signal_lamp_lit"_s);
        QCOMPARE(block(demo, 5, 1, 1), u"signal_lamp"_s);
        QCOMPARE(block(demo, 17, 1, 1), u"signal_lamp_lit"_s);
        QCOMPARE(property(demo, 21, 1, 1, u"powered"_s), u"true"_s);
        QCOMPARE(block(demo, 23, 1, 1), u"signal_lamp_lit"_s);
        QCOMPARE(property(demo, 26, 1, 1, u"open"_s), u"true"_s);
        QCOMPARE(property(demo, 29, 1, 1, u"open"_s), u"true"_s);
        // The cross next to the lever: 15 at its arm, 14 in the middle; the dot points nowhere.
        QCOMPARE(property(demo, 10, 1, 5, u"power"_s), u"15"_s);
        QCOMPARE(property(demo, 11, 1, 5, u"power"_s), u"14"_s);
        QCOMPARE(property(demo, 11, 1, 5, u"north"_s), u"side"_s);
        QCOMPARE(property(demo, 0, 1, 5, u"north"_s), u"none"_s);
        QCOMPARE(property(demo, 7, 1, 5, u"power"_s), u"0"_s);
        QCOMPARE(property(demo, 14, 1, 5, u"east"_s), u"up"_s);
    }

    void quirksHappen()
    {
        const QString demo = u"demo-signal_quirks"_s;
        visit(demo);
        QCOMPARE(property(demo, 8, 1, 4, u"extended"_s), u"false"_s);
        QCOMPARE(property(demo, 12, 1, 4, u"extended"_s), u"true"_s);
        QCOMPARE(block(demo, 22, 1, 5), u"gold_block"_s);
        QCOMPARE(property(demo, 20, 1, 5, u"extended"_s), u"false"_s);
        QCOMPARE(block(demo, 22, 1, 8), u"gold_block"_s);
        QCOMPARE(property(demo, 20, 1, 8, u"extended"_s), u"true"_s);
        // The clock burnt out within its first sixty ticks.
        QCOMPARE(block(demo, 2, 2, 2), u"signal_torch_off"_s);
        QVERIFY(m_server->circuitState().burntOutUntil.contains(origin(demo).offset(2, 2, 2)));
    }

    void pistonsMoveDoorsChestsAndStands()
    {
        const QString demo = u"demo-signal_pistons"_s;
        visit(demo);
        QCOMPARE(block(demo, 3, 1, 3), u"polished_stone"_s);
        QCOMPARE(block(demo, 3, 2, 3), u"polished_stone"_s);
        QCOMPARE(block(demo, 9, 1, 3), u"air"_s);
        QCOMPARE(block(demo, 9, 2, 3), u"air"_s);
        QCOMPARE(block(demo, 17, 1, 3), u"chest"_s);
        const QcContainerManager::Container *chest = m_server->containers().find(origin(demo).offset(17, 1, 3));
        QVERIFY(chest);
        QCOMPARE(chest->stacks[0].count, 5);
        QVERIFY(!m_server->containers().find(origin(demo).offset(16, 1, 3)));
        const QcBlockPos standCell = origin(demo).offset(17, 1, 3);
        const auto stand = std::ranges::find_if(m_server->stands().stands(), [&](const QcArmourStandManager::Stand &candidate) {
            return std::abs(candidate.position.z - (standCell.z + 0.5)) < 0.01 && candidate.position.y == standCell.y;
        });
        QVERIFY(stand != m_server->stands().stands().end());
        QVERIFY(stand->position.x > standCell.x + 1.0);
        QCOMPARE(block(demo, 38, 1, 8), u"signal_lamp_lit"_s);
    }
};

QTEST_GUILESS_MAIN(TestSignalShowcase)
#include "test_signal_showcase.moc"

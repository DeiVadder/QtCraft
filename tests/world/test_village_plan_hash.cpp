#include "common/content/loader/QcBaseContent.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/villages/QcVillageGenerator.h"

#include <QCryptographicHash>
#include <QMap>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;

// The village plans stay the same (decision E-419): the plans of 33 x 33 regions for three seeds,
// every field of them hashed, match the hashes recorded before the planner was split into its road
// network, piece placement and water parts. A change of the base content or of the planning rules
// changes them on purpose: then record the new hashes the failure prints.
class TestVillagePlanHash : public QObject
{
    Q_OBJECT

private:
    // Regions -rings..rings in both directions.
    static constexpr int rings = 16;

    std::shared_ptr<const QcContentRegistry> m_content = [] {
        QStringList warnings;
        return *QcBaseContent::load(warnings);
    }();

    static void addBlockPos(QCryptographicHash &hash, QcBlockPos pos)
    {
        const std::array<qint32, 3> values{pos.x, pos.y, pos.z};
        hash.addData(QByteArrayView(reinterpret_cast<const char *>(values.data()), qsizetype(sizeof(values))));
    }

    static void addInts(QCryptographicHash &hash, std::initializer_list<qint64> values)
    {
        for (const qint64 value : values) {
            hash.addData(QByteArrayView(reinterpret_cast<const char *>(&value), qsizetype(sizeof(value))));
        }
    }

    static void addString(QCryptographicHash &hash, const QString &text)
    {
        hash.addData(text.toUtf8());
        addInts(hash, {text.size()});
    }

    static void addStructure(QCryptographicHash &hash, const QcStructure &structure)
    {
        addString(hash, structure.name());
        addInts(hash, {structure.sizeX(), structure.sizeY(), structure.sizeZ()});
        for (int y = 0; y < structure.sizeY(); ++y) {
            for (int z = 0; z < structure.sizeZ(); ++z) {
                for (int x = 0; x < structure.sizeX(); ++x) {
                    const std::optional<QcBlockId> block = structure.block(x, y, z);
                    addInts(hash, {block ? qint64(*block) : -1});
                }
            }
        }
    }

    static void addPieces(QCryptographicHash &hash, const QcVillagePlan &plan)
    {
        addInts(hash, {plan.pieces.size()});
        for (const QcStructurePlacement &piece : plan.pieces) {
            addStructure(hash, piece.structure());
            addBlockPos(hash, piece.origin());
            addInts(hash, {piece.turns()});
        }
    }

    static void addCells(QCryptographicHash &hash, const QcVillagePlan &plan)
    {
        for (const QList<QcBlockPos> *cells : {&plan.roads, &plan.branchRoads, &plan.thresholds}) {
            addInts(hash, {cells->size()});
            for (const QcBlockPos &pos : *cells) {
                addBlockPos(hash, pos);
            }
        }
        for (const QcVillagePlan::Run &run : plan.runs) {
            addInts(hash, {run.x, run.z, run.dx, run.dz, run.length, run.arm, run.branch ? 1 : 0});
        }
        for (const QcVillagePlan::Step &step : plan.steps) {
            addBlockPos(hash, step.at);
            addInts(hash, {step.block});
        }
        for (const QcVillagePlan::Deck &deck : plan.decks) {
            addBlockPos(hash, deck.at);
            addInts(hash, {deck.block, deck.post});
        }
        for (const QList<QcVillagePlan::Area> *areas : {&plan.wetland.wet, &plan.wetland.dry}) {
            addInts(hash, {areas->size()});
            for (const QcVillagePlan::Area &area : *areas) {
                addInts(hash, {area.x0, area.z0, area.x1, area.z1});
            }
        }
        QList<QcBlockPos> fields(plan.fields.cbegin(), plan.fields.cend());
        std::ranges::sort(fields, [](QcBlockPos a, QcBlockPos b) { return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z); });
        for (const QcBlockPos &pos : std::as_const(fields)) {
            addBlockPos(hash, pos);
        }
    }

    static void addLife(QCryptographicHash &hash, const QcVillagePlan &plan)
    {
        for (const QList<QcVillagePlan::Spot> *spots : {&plan.beds, &plan.workstations}) {
            addInts(hash, {spots->size()});
            for (const QcVillagePlan::Spot &spot : *spots) {
                addBlockPos(hash, spot.at);
                addString(hash, spot.kind);
            }
        }
        for (const QcVillagePlan::Mob &mob : plan.mobs) {
            addString(hash, mob.mob);
            addInts(hash, {qint64(mob.at.x * 1024.0), qint64(mob.at.y * 1024.0), qint64(mob.at.z * 1024.0)});
        }
    }

    static void addPlan(QCryptographicHash &hash, const QcVillagePlan &plan)
    {
        addInts(hash, {plan.id, plan.minX, plan.minZ, plan.maxX, plan.maxZ, plan.roadLength, plan.winding ? 1 : 0});
        addString(hash, plan.style);
        addString(hash, plan.layout);
        addBlockPos(hash, plan.centre);
        addInts(hash, {plan.pathBlock, plan.branchPathBlock, plan.foundation, plan.stiltPost, plan.stiltSpacing});
        addPieces(hash, plan);
        addCells(hash, plan);
        addLife(hash, plan);
    }

    // The hash of all plans of the seed's regions, and how many regions have a village; prints the
    // villages per style and layout.
    std::pair<QByteArray, int> hashOf(qint64 seed) const
    {
        QMap<QString, int> kinds;
        const QcTerrainGenerator generator(m_content, quint64(seed));
        QCryptographicHash hash(QCryptographicHash::Sha256);
        int villages = 0;
        for (int rz = -rings; rz <= rings; ++rz) {
            for (int rx = -rings; rx <= rings; ++rx) {
                const std::shared_ptr<const QcVillagePlan> plan = generator.villages().regionPlan(rx, rz);
                addInts(hash, {rx, rz, plan ? 1 : 0});
                if (plan) {
                    addPlan(hash, *plan);
                    ++villages;
                    ++kinds[plan->style + u' ' + plan->layout];
                }
            }
        }
        for (auto it = kinds.cbegin(); it != kinds.cend(); ++it) {
            qInfo().noquote() << u"  %1: %2"_s.arg(it.key()).arg(it.value());
        }
        return {hash.result().toHex(), villages};
    }

private slots:
    void plansStayTheSame_data()
    {
        QTest::addColumn<qint64>("seed");
        QTest::addColumn<QByteArray>("expected");
        QTest::addColumn<int>("expectedVillages");
        QTest::newRow("seed 42") << qint64(42) << QByteArray("56c40787b0cc53d6ae83801273b64e39b3a73ae489ca921c2643a555fcffd7ea") << 176;
        QTest::newRow("seed 7") << qint64(7) << QByteArray("fabaaffdc6329f44040ba1d51c9885e793bd119e05bca4bfc3c336a16f55c899") << 171;
        QTest::newRow("seed 20261017") << qint64(20261017) << QByteArray("3b155dfe0d61ae1633ff1b4d4d6d82b54325a0c481df80c9ad71bffb22116629") << 169;
    }

    void plansStayTheSame()
    {
        QFETCH(qint64, seed);
        QFETCH(QByteArray, expected);
        QFETCH(int, expectedVillages);
        const auto [hash, villages] = hashOf(seed);
        qInfo().noquote() << u"seed %1: %2 villages, plans %3"_s.arg(seed).arg(villages).arg(QString::fromLatin1(hash));
        QCOMPARE(villages, expectedVillages);
        QCOMPARE(hash, expected);
    }
};

QTEST_GUILESS_MAIN(TestVillagePlanHash)
#include "test_village_plan_hash.moc"

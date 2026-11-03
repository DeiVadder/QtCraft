// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/entitymeshes/QcEntityMesher.h"
#include "clientcore/figures/QcSwimmerPose.h"
#include "clientcore/session/QcClientEntity.h"
#include "clientcore/session/QcInkClouds.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/content/registry/QcContentSerializer.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcEntityMessages.h"
#include "fixtures/QcTestUtil.h"

#include <QDataStream>
#include <QTest>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// Vertices of one box in QcEntityMesher's meshes: six faces of four corners.
constexpr qsizetype boxVertices = 24;

// The lowest and the farthest (from the vertical axis) point of the box at `index` of `mesh`.
struct Reach
{
    double lowest = 0.0;
    double farthest = 0.0;
};

Reach reachOf(const QcEntityMesh &mesh, qsizetype index)
{
    Reach reach{1e9, 0.0};
    for (qsizetype v = index * boxVertices; v < (index + 1) * boxVertices; ++v) {
        const QcEntityVertex &vertex = mesh.vertices.at(v);
        reach.lowest = std::min(reach.lowest, static_cast<double>(vertex.y));
        reach.farthest = std::max(reach.farthest, std::hypot(static_cast<double>(vertex.x), static_cast<double>(vertex.z)));
    }
    return reach;
}

} // namespace

// The look of squids on the client (decision E-974): the pulse and ink travel with the content,
// the tentacles open as the squid slows down, the body tilts, and a hurt squid leaves a cloud of
// ink that fades.
class TestSquidLooks : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<QcContentRegistry> m_content = QcTestUtil::loadContent();

    QcCreatureKindId squidKind() const { return *m_content->mobKind(u"qtcraft:squid"_s); }
    const QcMobDefinition &squid() const { return m_content->mobs()[squidKind()]; }

    static QcEntityMesher::PartLook spreadBy(double degrees)
    {
        QcEntityMesher::PartLook look;
        look.spread = degrees;
        return look;
    }

    static QcEntityMesher::PartLook tiltedBy(double degrees, double pivot)
    {
        QcEntityMesher::PartLook look;
        look.tilt = degrees;
        look.tiltHeight = pivot;
        return look;
    }

    QcEntityMesh meshOf(const QcEntityMesher::PartLook &look) const
    {
        QcEntityMesh mesh;
        QcEntityMesher::appendParts(mesh, squid().parts, QcEntityMesher::Placement{}, [](const QString &) { return QRectF(0, 0, 1, 1); }, look);
        return mesh;
    }

    qsizetype firstTentacle() const { return std::ranges::find_if(squid().parts, &QcMobPart::tentacle) - squid().parts.begin(); }

    QcClientEntity squidEntity(quint32 id) const
    {
        QcClientEntity entity;
        entity.id = id;
        entity.kind = QcEntityKind::Creature;
        entity.creatureType = squidKind();
        return entity;
    }

private slots:
    void pulseAndInkTravelWithTheContent()
    {
        QByteArray bytes;
        {
            QDataStream out(&bytes, QIODevice::WriteOnly);
            QcContentSerializer::write(*m_content, out);
        }
        QDataStream in(bytes);
        const QcResult<QcContentRegistry> copy = QcContentSerializer::read(in);
        QVERIFY2(copy.has_value(), qcErrorText(copy).constData());
        const QcMobDefinition &received = copy->mobs()[*copy->mobKind(u"qtcraft:squid"_s)];
        QCOMPARE(received.pulse, squid().pulse);
        QCOMPARE(received.ink, squid().ink);
        QCOMPARE(std::ranges::count_if(received.parts, &QcMobPart::tentacle), 8);
        // A fish swims steadily and leaves no ink.
        const QcMobDefinition &cod = copy->mobs()[*copy->mobKind(u"qtcraft:cod"_s)];
        QVERIFY(!cod.pulse && cod.ink.isEmpty());
    }

    // Spread, a tentacle reaches out and up from where it hangs; the mantle stays.
    void tentaclesOpenOutwards()
    {
        const qsizetype tentacle = firstTentacle();
        const QcEntityMesh closed = meshOf({});
        const QcEntityMesh open = meshOf(spreadBy(50.0));
        QCOMPARE(open.vertices.size(), closed.vertices.size());
        QVERIFY(reachOf(open, tentacle).farthest > reachOf(closed, tentacle).farthest + 0.3);
        QVERIFY(reachOf(open, tentacle).lowest > reachOf(closed, tentacle).lowest + 0.1);
        QCOMPARE(reachOf(open, 0).farthest, reachOf(closed, 0).farthest);
    }

    // Tilted by 90 degrees, the top of the mantle points north (-z) at the middle's height.
    void theBodyTiltsItsTopForwards()
    {
        const QcEntityMesh lying = meshOf(tiltedBy(90.0, 0.4));
        double north = 0.0;
        double highest = -1.0;
        for (qsizetype v = 0; v < boxVertices; ++v) {
            north = std::min(north, static_cast<double>(lying.vertices.at(v).z));
            highest = std::max(highest, static_cast<double>(lying.vertices.at(v).y));
        }
        // The mantle reaches 1 m up, 0.6 m above the pivot: now 0.6 m to the north, and its
        // 0.3 m half width is its height around the pivot.
        QVERIFY2(std::abs(north + 0.6) < 1e-3, qPrintable(QString::number(north)));
        QVERIFY2(std::abs(highest - 0.7) < 1e-3, qPrintable(QString::number(highest)));
    }

    // The opening follows the speed: pulled in at the speed of a push, open at rest; the hook
    // `--pose pulse:<share>` holds it.
    void theTentaclesFollowTheSpeed()
    {
        const double push = squid().pulse->speed;
        QCOMPARE(QcSwimmerPose::opening(push, push, 0.0, 0.0), 0.0);
        QVERIFY(QcSwimmerPose::opening(0.0, push, 0.0, 0.0) > 0.8);
        QVERIFY(QcSwimmerPose::opening(push * 0.3, push, 0.0, 0.0) > QcSwimmerPose::opening(push * 0.7, push, 0.0, 0.0));
        QCOMPARE(QcSwimmerPose::forcedOpening(u"pulse:0.25"_s), std::optional(0.25));
        QCOMPARE(QcSwimmerPose::forcedOpening(u"walk:0.3"_s), std::nullopt);

        QcClientEntity entity = squidEntity(7);
        entity.previousPitch = 40.0f;
        entity.pitch = 80.0f;
        entity.position = {push * QcPhysics::tickDuration, 0.0, 0.0};
        const QcSwimmerPose::Look pushing = QcSwimmerPose::of(squid(), entity, 0.5, 0.0, std::nullopt);
        QCOMPARE(pushing.tilt, 60.0);
        QCOMPARE(pushing.height, squid().height * 0.5);
        QCOMPARE(pushing.spread, 0.0);
        entity.position = {};
        QCOMPARE(QcSwimmerPose::of(squid(), entity, 0.5, 0.0, QcSwimmerPose::forcedOpening(u"pulse:1"_s)).spread, squid().pulse->spread);
    }

    // A struck squid leaves its puffs of ink around its middle as drawn (not where the newest move
    // puts it) that drift, swell, shrink and go; a squid hurt without a blow (suffocating), a
    // struck cod and an unhurt squid leave none.
    void aStruckSquidLeavesACloudOfInk()
    {
        QHash<quint32, QcClientEntity> entities;
        QcClientEntity shown = squidEntity(7);
        shown.position = {1.0, 50.0, 1.0};
        entities.insert(7, shown);
        QcClientEntity cod = squidEntity(8);
        cod.creatureType = *m_content->mobKind(u"qtcraft:cod"_s);
        entities.insert(8, cod);
        QcMsg::EntityMoves moves;
        moves.moves = {{.id = 7, .position = {3.0, 50.0, 3.0}, .hurt = true, .struck = false}, {.id = 8, .position = {}, .hurt = true, .struck = true}};
        QcInkClouds ink;
        ink.addStruck(moves, entities, *m_content, 100);
        QVERIFY(ink.puffs().isEmpty());

        moves.moves.first().struck = true;
        ink.addStruck(moves, entities, *m_content, 100);
        QCOMPARE(ink.puffs().size(), QcInkClouds::puffsPerCloud);
        const QcVec3 middle{1.0, 50.0 + squid().height * 0.5, 1.0};
        for (const QcInkClouds::Puff &puff : ink.puffs()) {
            QCOMPARE(puff.texture, u"qtcraft:squid_ink"_s);
            QCOMPARE(puff.centre, middle);
            const std::optional<QcInkClouds::Look> start = QcInkClouds::lookAt(puff, 100.0);
            const std::optional<QcInkClouds::Look> later = QcInkClouds::lookAt(puff, 106.0);
            QVERIFY(start && later && later->size > start->size);
            QVERIFY((later->position - middle).length() > 0.05 && (later->position - middle).length() < 1.5);
            QVERIFY(!QcInkClouds::lookAt(puff, 100.0 + puff.lifeTicks));
        }
        ink.prune(100 + QcInkClouds::maxLifeTicks);
        QVERIFY(ink.puffs().isEmpty());

        // Many hits at once add no more than the bound.
        for (quint64 tick = 0; tick < 100; ++tick) {
            ink.add({}, squid().ink, 7, tick);
        }
        QVERIFY(ink.puffs().size() <= QcInkClouds::maxPuffs);
    }
};

QTEST_GUILESS_MAIN(TestSquidLooks)
#include "test_squid_looks.moc"

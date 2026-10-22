#include "clientcore/meshing/QcEntityCulling.h"
#include "clientcore/session/QcClientEntity.h"

#include <QTest>

namespace {

// A camera at the origin, eyes at y = 1.6, looking north (-z), level; with a 2:1 view the sides of
// the view run about 56 degrees left and right of the view direction (about 15 m to the side at
// 10 m).
QcCamera northCamera()
{
    QcCamera camera;
    camera.position = {0.0f, 1.6f, 0.0f};
    return camera;
}

std::optional<QcFrustum::Planes> northPlanes()
{
    return QcEntityCulling::viewPlanes(northCamera(), 1000.0, 500.0);
}

QcClientEntity standingAt(const QcVec3 &feet)
{
    QcClientEntity entity;
    entity.kind = QcEntityKind::Player;
    entity.position = feet;
    entity.previousPosition = feet;
    return entity;
}

bool seen(const std::optional<QcFrustum::Planes> &planes, const QcClientEntity &entity)
{
    const QcContentRegistry content;
    return QcEntityCulling::mayBeSeen(planes, QcEntityCulling::viewBox(entity, content));
}

} // namespace

class TestEntityCulling : public QObject
{
    Q_OBJECT

private slots:
    void inFrontIsBuiltBehindIsNot()
    {
        const std::optional<QcFrustum::Planes> planes = northPlanes();
        QVERIFY(planes);
        QVERIFY(seen(planes, standingAt({0.0, 0.0, -10.0})));
        QVERIFY(!seen(planes, standingAt({0.0, 0.0, 10.0})));
        QVERIFY(!seen(planes, standingAt({30.0, 0.0, -10.0})));
    }

    void heldItemReachKeepsABodyJustBesideTheView()
    {
        // The body (x 17 to 18, z -10.5 to -9.5) lies beside the view (edge near x 15.5 there),
        // what it holds may not; a body 6.5 m further out is beyond the margin.
        const std::optional<QcFrustum::Planes> planes = northPlanes();
        QVERIFY(seen(planes, standingAt({17.5, 0.0, -10.0})));
        QVERIFY(!seen(planes, standingAt({24.0, 0.0, -10.0})));
    }

    void aFastEntityCountsWhereTheTickStarted()
    {
        // A projectile leaving the view this tick is still drawn on the way out.
        QcClientEntity arrow = standingAt({40.0, 1.0, -10.0});
        arrow.kind = QcEntityKind::Projectile;
        QVERIFY(!seen(northPlanes(), arrow));
        arrow.previousPosition = {0.0, 1.0, -10.0};
        QVERIFY(seen(northPlanes(), arrow));
    }

    void aPaintingCountsWithItsWholeBox()
    {
        // A wide painting on a wall north of the camera reaching into the view from its left; the
        // position does not matter.
        QcClientEntity painting = standingAt({0.0, 0.0, 100.0});
        painting.kind = QcEntityKind::Hanging;
        painting.hanging.type = QcHangingType::Painting;
        painting.hanging.face = QcBlockFace::South;
        painting.hanging.width = 30;
        painting.hanging.height = 2;
        painting.hanging.cell = {-40, 0, -11};
        const QcAabb box = painting.hanging.box();
        QVERIFY2(box.min.x < -14.0 && box.max.x > -14.0, "the painting crosses the left edge of the view");
        QVERIFY(seen(northPlanes(), painting));

        painting.hanging.cell = {-40, 0, 11};
        QVERIFY(!seen(northPlanes(), painting));
    }

    void withoutASizedViewEverythingIsBuilt()
    {
        const std::optional<QcFrustum::Planes> none = QcEntityCulling::viewPlanes(northCamera(), 0.0, 500.0);
        QVERIFY(!none);
        QVERIFY(seen(none, standingAt({0.0, 0.0, 10.0})));
    }
};

QTEST_GUILESS_MAIN(TestEntityCulling)
#include "test_entity_culling.moc"

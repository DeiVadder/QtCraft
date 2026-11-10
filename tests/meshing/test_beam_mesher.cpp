// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/entitymeshes/QcBeamMesher.h"
#include "clientcore/entitymeshes/QcEntityVertex.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <numbers>

// The beam of a beacon as meshes (decision E-1143): the core and the glow, their quads, heights,
// texture segments and box, nothing for a camera inside, faces that look out.
class TestBeamMesher : public QObject
{
    Q_OBJECT

private:
    static constexpr QcBlockPos beacon{10, 64, -3};
    static inline const QRectF tile{0.25, 0.5, 0.125, 0.125};

    // A beam of a white section of 3 blocks and a red one of 5.
    static QcMsg::BeaconBeam beam() { return {beacon, {{0xFFFFFF, 3}, {0xFF0000, 5}}}; }

    // A camera well outside the beam.
    static QcVec3 outside() { return {beacon.x + 5.5, beacon.y + 2.0, beacon.z + 0.5}; }

    static QcEntityMesh meshAt(const QcVec3 &origin)
    {
        QcEntityMesh mesh;
        mesh.origin = origin;
        return mesh;
    }

    static float lowest(const QcEntityMesh &mesh)
    {
        return std::ranges::min(mesh.vertices, {}, &QcEntityVertex::y).y;
    }

    static float highest(const QcEntityMesh &mesh)
    {
        return std::ranges::max(mesh.vertices, {}, &QcEntityVertex::y).y;
    }

private slots:
    void theCoreRunsFromTheBeaconPastItsLastSection()
    {
        QcEntityMesh mesh = meshAt({});
        QcBeamMesher::appendCore(mesh, beam(), outside(), 0.0, tile);
        const int height = QcBeamMesher::heightOf(beam());
        QCOMPARE(height, 3 + 5 + QcBeamMesher::above);
        // At time 0 the segments start at the beacon: 2 + 3 + 128 (the last section with the
        // 256 blocks above) segments, of four faces each, one side only.
        const qsizetype segments = 2 + 3 + 128;
        QCOMPARE(mesh.vertices.size(), segments * 4 * 4);
        QCOMPARE(mesh.indices.size(), segments * 4 * 6);
        QCOMPARE(lowest(mesh), static_cast<float>(beacon.y));
        QCOMPARE(highest(mesh), static_cast<float>(beacon.y + height));
        for (const QcEntityVertex &vertex : std::as_const(mesh.vertices)) {
            QCOMPARE(vertex.blockLight, QcEntityVertex::emissiveLight);
            QVERIFY(vertex.u >= tile.left() && vertex.u <= tile.right());
            QVERIFY(std::hypot(vertex.x - (beacon.x + 0.5), vertex.z - (beacon.z + 0.5)) <= QcBeamMesher::halfWidth * std::numbers::sqrt2 + 1e-5);
        }
        // The red section's colour reaches the core in full.
        QVERIFY(std::ranges::any_of(mesh.vertices, [](const QcEntityVertex &vertex) { return vertex.tint[0] == 255 && vertex.tint[1] == 0; }));
    }

    void theTextureMovesUpInSegments()
    {
        // Half a segment later the first piece is half a tile.
        QcEntityMesh moved = meshAt({});
        QcBeamMesher::appendCore(moved, beam(), outside(), QcBeamMesher::segmentLength / 2.0 / QcBeamMesher::scrollPerSecond, tile);
        const float firstUp = moved.vertices[2].u - moved.vertices[0].u;
        QVERIFY(std::abs(firstUp) < tile.width() * 0.75);
    }

    void theFacesLookOut()
    {
        QcEntityMesh mesh = meshAt({});
        QcBeamMesher::appendCore(mesh, beam(), outside(), 0.0, tile);
        // Each quad's triangle 0 1 2 turns counter-clockwise seen from outside: its normal points
        // away from the axis.
        for (qsizetype index = 0; index + 2 < mesh.indices.size(); index += 6) {
            const QcEntityVertex &a = mesh.vertices[mesh.indices[index]];
            const QcEntityVertex &b = mesh.vertices[mesh.indices[index + 1]];
            const QcEntityVertex &c = mesh.vertices[mesh.indices[index + 2]];
            const double abx = b.x - a.x;
            const double abz = b.z - a.z;
            const double acx = c.x - a.x;
            const double acy = c.y - a.y;
            const double acz = c.z - a.z;
            const double abY = b.y - a.y;
            const double normalX = abY * acz - abz * acy;
            const double normalZ = abx * acy - abY * acx;
            const double outX = (a.x + b.x) / 2.0 - (beacon.x + 0.5);
            const double outZ = (a.z + b.z) / 2.0 - (beacon.z + 0.5);
            QVERIFY(normalX * outX + normalZ * outZ > 0.0);
        }
    }

    void theGlowIsWiderFainterAndStill()
    {
        QcEntityMesh first = meshAt({});
        QcBeamMesher::appendGlow(first, beam(), outside(), 0.0, tile);
        QcEntityMesh later = meshAt({});
        QcBeamMesher::appendGlow(later, beam(), outside(), 0.3, tile);
        QVERIFY(!first.vertices.isEmpty());
        // It does not turn: its corners stay where they are.
        QCOMPARE(first.vertices.first().x, later.vertices.first().x);
        QCOMPARE(first.vertices.first().z, later.vertices.first().z);
        const float reach = std::ranges::max(first.vertices, {}, &QcEntityVertex::x).x - static_cast<float>(beacon.x + 0.5);
        // Not turned, its square stands along the axes: its faces lie glowHalfWidth from the axis.
        QVERIFY(std::abs(reach - QcBeamMesher::glowHalfWidth) < 1e-4);
        // An eighth of the white section's colour.
        QCOMPARE(first.vertices.first().tint[0], static_cast<quint8>(std::lround(255 * QcBeamMesher::glowStrength)));
    }

    void aCameraInsideAColumnGetsNoneOfIt()
    {
        const QcVec3 inside{beacon.x + 0.5, beacon.y + 10.0, beacon.z + 0.5};
        QcEntityMesh core = meshAt({});
        QcBeamMesher::appendCore(core, beam(), inside, 0.0, tile);
        QVERIFY(core.vertices.isEmpty());
        QcEntityMesh glow = meshAt({});
        QcBeamMesher::appendGlow(glow, beam(), inside, 0.0, tile);
        QVERIFY(glow.vertices.isEmpty());

        // Between the core and the glow's edge: the core shows, the glow does not.
        const QcVec3 between{beacon.x + 0.5 + 0.42, beacon.y + 10.0, beacon.z + 0.5};
        QcBeamMesher::appendCore(core, beam(), between, 0.0, tile);
        QcBeamMesher::appendGlow(glow, beam(), between, 0.0, tile);
        QVERIFY(!core.vertices.isEmpty());
        QVERIFY(glow.vertices.isEmpty());
    }

    void theBoxHoldsTheGlow()
    {
        const QcAabb box = QcBeamMesher::boxOf(beam());
        const double radius = QcBeamMesher::glowHalfWidth * std::numbers::sqrt2;
        QCOMPARE(box.min.y, static_cast<double>(beacon.y));
        QCOMPARE(box.max.y, static_cast<double>(beacon.y + QcBeamMesher::heightOf(beam())));
        QVERIFY(std::abs(box.min.x - (beacon.x + 0.5 - radius)) < 1e-9);
        QVERIFY(std::abs(box.max.z - (beacon.z + 0.5 + radius)) < 1e-9);
    }
};

QTEST_GUILESS_MAIN(TestBeamMesher)
#include "test_beam_mesher.moc"

// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/core/QcBlockPos.h"
#include "common/world/QcLightVolume.h"
#include "QcTestWorld.h"

#include <QTest>

#include <cstdlib>

using namespace Qt::StringLiterals;

namespace {

int manhattan(QcBlockPos a, QcBlockPos b)
{
    return std::abs(a.x - b.x) + std::abs(a.y - b.y) + std::abs(a.z - b.z);
}

// Test registry: stone and water from qcMakeTestContent plus a torch (light 14) and a glowing block.
struct Content
{
    QcContentRegistry registry = qcMakeTestContent();
    QcBlockId stone = *registry.blockId(u"test:stone"_s);
    QcBlockId torch = 0;
    QcBlockId lamp = 0;

    Content()
    {
        QcBlockDefinition definition;
        definition.name = u"test:torch"_s;
        definition.solid = false;
        definition.opaque = false;
        definition.renderLayer = QcRenderLayer::Cutout;
        definition.lightOpacity = 0;
        definition.lightLevel = 14;
        definition.faceTextures.fill(u"test:torch"_s);
        torch = *registry.registerBlock(definition);

        definition.name = u"test:lamp"_s;
        definition.solid = true;
        definition.opaque = true;
        definition.renderLayer = QcRenderLayer::Opaque;
        definition.lightOpacity = 15;
        definition.lightLevel = 15;
        lamp = *registry.registerBlock(definition);
    }
};

} // namespace

class TestBlockLight : public QObject
{
    Q_OBJECT

private:
    Content m_content;

private slots:
    void lightFallsOffOneLevelPerBlock()
    {
        constexpr int size = 31;
        QcLightVolume volume(size, size, size);
        volume.addSource(15, 15, 15, 14);
        volume.propagate();

        for (int x = 0; x < size; ++x) {
            for (int y = 0; y < size; ++y) {
                for (int z = 0; z < size; ++z) {
                    const int expected = std::max(0, 14 - manhattan({x, y, z}, {15, 15, 15}));
                    if (volume.level(x, y, z) != expected) {
                        QFAIL(qPrintable(u"level at %1 %2 %3 is %4, expected %5"_s.arg(x).arg(y).arg(z).arg(volume.level(x, y, z)).arg(expected)));
                    }
                }
            }
        }

        QCOMPARE(volume.level(-1, 0, 0), 0);
    }

    void wallsBlockAndDenseCellsDim()
    {
        // A complete wall at x = 10 keeps the far side dark.
        QcLightVolume walled(20, 5, 5);
        for (int y = 0; y < 5; ++y) {
            for (int z = 0; z < 5; ++z) {
                walled.setOpacity(10, y, z, 15);
            }
        }

        walled.addSource(8, 2, 2, 14);
        walled.propagate();
        QCOMPARE(walled.level(9, 2, 2), 13);
        QCOMPARE(walled.level(10, 2, 2), 0);
        QCOMPARE(walled.level(11, 2, 2), 0);

        // A cell of opacity 3 costs three levels; the next one again one.
        QcLightVolume dim(10, 1, 1);
        dim.setOpacity(3, 0, 0, 3);
        dim.addSource(0, 0, 0, 14);
        dim.propagate();
        QCOMPARE(dim.level(2, 0, 0), 12);
        QCOMPARE(dim.level(3, 0, 0), 9);
        QCOMPARE(dim.level(4, 0, 0), 8);

        // A source shines in its own cell even when that cell is opaque (a glowing block).
        QcLightVolume glowing(5, 1, 1);
        glowing.setOpacity(0, 0, 0, 15);
        glowing.addSource(0, 0, 0, 15);
        glowing.propagate();
        QCOMPARE(glowing.level(0, 0, 0), 15);
        QCOMPARE(glowing.level(1, 0, 0), 14);
    }

    void theBrighterSourceWins()
    {
        QcLightVolume volume(30, 1, 1);
        volume.addSource(0, 0, 0, 14);
        volume.addSource(20, 0, 0, 10);
        volume.propagate();
        for (int x = 0; x < 30; ++x) {
            const int expected = std::max({0, 14 - x, 10 - std::abs(x - 20)});
            QCOMPARE(volume.level(x, 0, 0), expected);
        }
    }

    void lightCrossesChunkBordersAndGoesAway() const
    {
        QcTestWorld world(m_content.registry);
        // A torch in chunk 0 next to its east border lights chunk 1.
        world.set({15, 64, 3}, m_content.torch);
        QCOMPARE(QcLightVolume::blockLightAt(world, {15, 64, 3}), 14);
        QCOMPARE(QcLightVolume::blockLightAt(world, {16, 64, 3}), 13);
        QCOMPARE(QcLightVolume::blockLightAt(world, {19, 65, 1}), 14 - 4 - 1 - 2);
        QCOMPARE(QcLightVolume::blockLightAt(world, {-2, 64, 3}), 0);
        QCOMPARE(QcLightVolume::blockLightAt(world, {28, 64, 3}), 1);
        QCOMPARE(QcLightVolume::blockLightAt(world, {29, 64, 3}), 0);

        // A wall along the border with no way around within reach darkens the other side.
        world.fillBox({16, 40, -30}, {16, 90, 30}, m_content.stone);
        QCOMPARE(QcLightVolume::blockLightAt(world, {17, 64, 3}), 0);

        // A gap in the wall: light goes through it and spreads again.
        world.set({16, 64, 5}, qcAirBlockId);
        QCOMPARE(QcLightVolume::blockLightAt(world, {16, 64, 5}), 14 - 3);
        QCOMPARE(QcLightVolume::blockLightAt(world, {17, 64, 3}), 14 - 3 - 3);

        // Removing the torch removes its light everywhere.
        world.set({15, 64, 3}, qcAirBlockId);
        for (const QcBlockPos pos : {QcBlockPos{15, 64, 3}, QcBlockPos{16, 64, 5}, QcBlockPos{17, 64, 3}, QcBlockPos{10, 64, 3}}) {
            QCOMPARE(QcLightVolume::blockLightAt(world, pos), 0);
        }
    }

    void glowingBlocksAndUnloadedChunks() const
    {
        QcTestWorld world(m_content.registry);
        world.set({0, 64, 0}, m_content.lamp);
        QCOMPARE(QcLightVolume::blockLightAt(world, {0, 65, 0}), 14);
        QCOMPARE(QcLightVolume::blockLightAt(world, {3, 64, 0}), 12);
        // Unloaded space is dark and blocks the way.
        world.loadedRadius = 2;
        QCOMPARE(QcLightVolume::blockLightAt(world, {2, 64, 0}), 13);
        QCOMPARE(QcLightVolume::blockLightAt(world, {3, 64, 0}), 0);
    }
};

QTEST_GUILESS_MAIN(TestBlockLight)
#include "test_block_light.moc"

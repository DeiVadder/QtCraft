// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/entitymeshes/QcEntityMesher.h"
#include "clientcore/entitymeshes/QcSpriteShapes.h"
#include "clientcore/figures/QcFigureLook.h"
#include "clientcore/texturepacks/QcTexturePackLibrary.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/loader/QcBaseContent.h"
#include "common/content/registry/QcContentRegistry.h"

#include <QElapsedTimer>
#include <QSet>
#include <QTest>

#include <algorithm>
#include <memory>

using namespace Qt::StringLiterals;

// Headless measurement of the mesh of many dropped items (decision E-991), without a window and
// without real time: the frame builder's work for each drop (its look, its shape, its quads) over
// frames whose clock is stepped by hand, on the real atlas of the base content. Three crowds: every
// item in turn, only the items drawn from a sprite, and those as stacks of 16 (three copies).
// QTCRAFT_DROP_MEASURE=full takes 500 drops over 300 frames (the recorded size), else 50 over 5
// (every test run); QTCRAFT_DROP_MEASURE_PACK=<id> measures in that texture pack,
// QTCRAFT_DROP_MEASURE_RESOLUTION=<pixels> paints that (procedural) pack at another size, e.g. the
// HD pack at 256. Also the first build of every sprite's shape, as after a pack switch. Prints
// "MEASURE key=value ..." lines; the times are the median frame.
class TestDropMeasure : public QObject
{
    Q_OBJECT

    std::shared_ptr<QcContentRegistry> m_content = std::make_shared<QcContentRegistry>();
    std::shared_ptr<const QcTextureAtlas> m_atlas;
    QcSpriteShapes m_shapes;

    // Every item, or only those without a block's form.
    [[nodiscard]] QList<QcItemId> itemsOf(bool spritesOnly) const
    {
        QList<QcItemId> items;
        for (qsizetype index = 1; index <= m_content->itemCount(); ++index) {
            const auto item = static_cast<QcItemId>(index);
            if (!spritesOnly || !QcFigureLook::item(*m_content, item).cube) {
                items.append(item);
            }
        }
        return items;
    }

    [[nodiscard]] static QList<QcClientEntity> crowd(const QList<QcItemId> &items, int drops, int count)
    {
        QList<QcClientEntity> entities;
        for (int index = 0; index < drops; ++index) {
            const int row = index / 25;
            QcClientEntity entity;
            entity.id = static_cast<quint32>(index + 1);
            entity.item = {items[index % items.size()], count, 0};
            entity.position = {static_cast<double>(index % 25), 64.0, static_cast<double>(row)};
            entity.previousPosition = entity.position;
            entities.append(entity);
        }
        return entities;
    }

    // What QcEntityFrameBuilder does for the items of one frame.
    [[nodiscard]] QcEntityMesh frameOf(const QList<QcClientEntity> &entities, double seconds)
    {
        const QcEntityMesher::UvLookup uv = [this](const QString &texture) { return m_atlas->uv(texture); };
        QcEntityMesh mesh;
        for (const QcClientEntity &entity : entities) {
            QcEntityMesher::appendItem(mesh, entity, QcFigureLook::dropped(*m_content, entity.item.item, m_shapes, m_atlas), {1.0, seconds, 1.0f}, uv);
        }
        return mesh;
    }

    void measure(const QString &name, const QList<QcClientEntity> &entities, int frames)
    {
        QList<double> times;
        QcEntityMesh mesh;
        for (int frame = 0; frame < frames; ++frame) {
            QElapsedTimer timer;
            timer.start();
            mesh = frameOf(entities, frame / 60.0);
            times.append(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);
        }
        std::ranges::sort(times);
        const qint64 triangles = mesh.indices.size() / 3;
        const qint64 bytes = mesh.vertices.size() * static_cast<qint64>(sizeof(QcEntityVertex)) + mesh.indices.size() * 4;
        qInfo().noquote() << u"MEASURE crowd=%1 drops=%2 triangles=%3 triangles_per_drop=%4 vertices=%5 kb=%6 median_ms=%7 p95_ms=%8"_s
                                 .arg(name)
                                 .arg(entities.size())
                                 .arg(triangles)
                                 .arg(static_cast<double>(triangles) / static_cast<double>(entities.size()), 0, 'f', 1)
                                 .arg(mesh.vertices.size())
                                 .arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 0)
                                 .arg(times[times.size() / 2], 0, 'f', 3)
                                 .arg(times[times.size() * 95 / 100], 0, 'f', 3);
        QVERIFY(triangles >= entities.size());
    }

private slots:
    void initTestCase()
    {
        QVERIFY(QcBaseContent::loadInto(*m_content));
        QVERIFY(m_content->finish().isEmpty());
        const QString pack = qEnvironmentVariable("QTCRAFT_DROP_MEASURE_PACK");
        const QList<QcTexturePackLibrary::Entry> entries = QcTexturePackLibrary::scan(QcTexturePackLibrary::roots(QString()));
        QcTexturePackStack packs = pack.isEmpty() ? QcTexturePackStack() : QcTexturePackLibrary::loadStack({pack}, entries).stack;
        const int resolution = qEnvironmentVariableIntValue("QTCRAFT_DROP_MEASURE_RESOLUTION");
        if (resolution > 0 && !packs.isEmpty()) {
            auto painted = std::make_shared<QcTexturePack>(*packs.front());
            painted->manifest.resolution = resolution;
            packs.front() = painted;
        }
        // Room for 256 pixel tiles of the whole content (the default budget would halve them).
        QcTextureAtlas::Options options;
        options.maxExtent = 16384;
        options.memoryBudget = qint64{1024} * 1024 * 1024;
        m_atlas = std::make_shared<const QcTextureAtlas>(QcTextureAtlas::build(*m_content, packs, options));
        qInfo().noquote() << u"MEASURE pack=%1 tile=%2"_s.arg(pack).arg(m_atlas->tileSize());
    }

    void manyDrops()
    {
        const bool full = qEnvironmentVariable("QTCRAFT_DROP_MEASURE") == u"full"_s;
        const int drops = full ? 500 : 50;
        const int frames = full ? 300 : 5;
        const QList<QcItemId> all = itemsOf(false);
        const QList<QcItemId> sprites = itemsOf(true);
        measure(u"all"_s, crowd(all, drops, 1), frames);
        measure(u"sprites"_s, crowd(sprites, drops, 1), frames);
        measure(u"stacks"_s, crowd(sprites, drops, 16), frames);
    }

    // What a pack switch costs once: every sprite's shape cut from the new atlas and its form built.
    void shapesAfterAPackSwitch()
    {
        QSet<QString> textures;
        const QList<QcItemId> sprites = itemsOf(true);
        for (const QcItemId item : sprites) {
            textures.insert(QcFigureLook::item(*m_content, item).sprite);
        }
        QcSpriteShapes shapes;
        qint64 quads = 0;
        int size = 0;
        QElapsedTimer timer;
        timer.start();
        for (const QString &texture : std::as_const(textures)) {
            const std::shared_ptr<const QcSpriteShape> shape = shapes.shape(m_atlas, texture);
            quads += shape->form().size();
            size = shape->size();
        }
        const double ms = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
        qInfo().noquote() << u"MEASURE shapes=%1 shape_texels=%2 quads=%3 first_build_ms=%4"_s.arg(textures.size()).arg(size).arg(quads).arg(ms, 0, 'f', 1);
        QVERIFY(quads > 0);
    }
};

QTEST_GUILESS_MAIN(TestDropMeasure)
#include "test_drop_measure.moc"

// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/figures/QcFigureLook.h"
#include "clientcore/icons/QcBlockIcon.h"
#include "clientcore/icons/QcItemIconSet.h"
#include "clientcore/meshing/QcBlockRenderTable.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/blocks/QcBlockSerializer.h"
#include "common/content/registry/QcContentRegistry.h"
#include "fixtures/QcTestUtil.h"

#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// The colours of the opaque pixels of an icon.
QSet<QRgb> opaqueColours(const QImage &icon)
{
    QSet<QRgb> colours;
    const QImage image = icon.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) == 255) {
                colours.insert(image.pixel(x, y));
            }
        }
    }
    return colours;
}

} // namespace

// Icons of blocks that fill several cells (decision E-885): a bed shows its foot and its head, a
// figure of one part draws exactly like the single-state icon.
class TestBlockIcons : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Never the player's own data folder.
        QVERIFY(m_data.isValid());
        qputenv("QTCRAFT_DATA_DIR", m_data.path().toUtf8());
    }

    void onePartDrawsLikeTheStateIcon()
    {
        const auto content = QcTestUtil::baseContent();
        QVERIFY(content.has_value());
        const QcTextureAtlas atlas = QcTextureAtlas::build(*content->registry);
        const QcBlockRenderTable table(*content->registry, atlas);
        const std::array<QImage, 6> shaded = QcBlockIcon::shadedAtlases(atlas.levelForTile(QcBlockIcon::sourceTileSize));
        for (const char *name : {"qtcraft:stone", "qtcraft:cobblestone_stairs", "qtcraft:cobblestone_slab"}) {
            const std::optional<QcBlockId> block = content->registry->blockId(QString::fromLatin1(name));
            QVERIFY2(block.has_value(), name);
            const QcBlockRenderTable::Entry &entry = table.entry(QcFigureLook::itemState(*content->registry, *block));
            const QImage single = QcBlockIcon::render(entry, shaded);
            const QImage parts = QcBlockIcon::render(QList<QcBlockIcon::Part>{{&entry, {0, 0, 0}}}, shaded);
            QCOMPARE(parts, single);
            QVERIFY(!opaqueColours(single).isEmpty());
        }
    }

    void theBedIconShowsBothHalves()
    {
        const auto content = QcTestUtil::baseContent();
        QVERIFY(content.has_value());
        const QcContentRegistry &registry = *content->registry;
        const QcTextureAtlas atlas = QcTextureAtlas::build(registry);
        const QcBlockRenderTable table(registry, atlas);
        const std::array<QImage, 6> shaded = QcBlockIcon::shadedAtlases(atlas.levelForTile(QcBlockIcon::sourceTileSize));
        const std::optional<QcBlockId> bed = registry.blockId(u"qtcraft:bed"_s);
        QVERIFY(bed.has_value());
        const QcBlockId state = QcFigureLook::itemState(registry, *bed);
        const QImage foot = QcBlockIcon::render(table.entry(registry.withProperty(state, u"part"_s, u"foot"_s)), shaded);
        const QImage head = QcBlockIcon::render(table.entry(registry.withProperty(state, u"part"_s, u"head"_s)), shaded);

        const QHash<QString, QImage> icons = QcItemIconSet::draw(registry, atlas, &table, QcBlockIcon::iconSize);
        QVERIFY(icons.contains(u"qtcraft:bed"_s));
        const QImage icon = icons.value(u"qtcraft:bed"_s);
        QVERIFY(icon != foot);
        QVERIFY(icon != head);

        // Colours only the head has (the pillow) show although the foot is in front.
        const QSet<QRgb> headOnly = QSet<QRgb>(opaqueColours(head)).subtract(opaqueColours(foot));
        QVERIFY(!headOnly.isEmpty());
        QVERIFY(opaqueColours(icon).intersects(headOnly));

        // Two cells long, scaled into one: wider than tall, and wider in proportion than one half.
        const auto aspect = [](const QImage &image) {
            QRect bounds;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    if (image.pixelColor(x, y).alpha() > 0) {
                        bounds |= QRect(x, y, 1, 1);
                    }
                }
            }
            return static_cast<double>(bounds.width()) / static_cast<double>(std::max(1, bounds.height()));
        };
        QVERIFY2(aspect(icon) > aspect(foot) + 0.1, qPrintable(u"%1 vs %2"_s.arg(aspect(icon)).arg(aspect(foot))));
    }

    void theContentSyncKeepsTheBed()
    {
        // The client draws and predicts beds from the synced content: the flag goes over the wire
        // (protocol 79).
        const auto content = QcTestUtil::baseContent();
        QVERIFY(content.has_value());
        const QcContentRegistry &registry = *content->registry;
        const std::optional<QcBlockId> bed = registry.blockId(u"qtcraft:bed"_s);
        QVERIFY(bed.has_value());
        const QcBlockDefinition &definition = registry.block(*bed);
        QVERIFY(definition.bed);

        QByteArray bytes;
        {
            QDataStream out(&bytes, QIODevice::WriteOnly);
            QcBlockSerializer::writeBlock(out, definition, registry.shapeOf(definition));
        }
        QDataStream in(bytes);
        const std::optional<QcBlockDefinition> read = QcBlockSerializer::readBlock(in);
        QVERIFY(read.has_value());
        QVERIFY(read->bed);
        QVERIFY(!read->door);
    }

private:
    QTemporaryDir m_data;
};

QTEST_MAIN(TestBlockIcons)
#include "test_block_icons.moc"

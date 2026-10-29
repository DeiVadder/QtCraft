// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// The enchanting table's runes (decision E-884): the glyphs, how the screen writes a clue with them
// as the bookshelves grow, the pixel art the screens draw, and the book and runes in the world.
#include "app/models/QcPixelArtProvider.h"
#include "clientcore/session/QcEnchantingAura.h"
#include "clientcore/session/QcRuneScript.h"
#include "clientcore/settings/QcUiPalette.h"
#include "clientcore/textures/QcRuneGlyphs.h"

#include <QSet>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// The positions of the readable letters (not spaces) of `signs`.
QSet<int> readablePositions(const std::vector<QcRuneScript::Sign> &signs)
{
    QSet<int> positions;
    for (std::size_t index = 0; index < signs.size(); ++index) {
        if (signs[index].rune < 0 && signs[index].character != u" "_s) {
            positions.insert(static_cast<int>(index));
        }
    }
    return positions;
}

// A rune as a string of its pixels, to compare runes.
QString pixelsOf(int rune)
{
    QString pixels;
    for (int row = 0; row < QcRuneGlyphs::height; ++row) {
        for (int column = 0; column < QcRuneGlyphs::width; ++column) {
            pixels += QcRuneGlyphs::isSet(rune, column, row) ? u'#' : u'.';
        }
    }
    return pixels;
}

} // namespace

class TestRuneScript : public QObject
{
    Q_OBJECT

private slots:
    void runesAreDistinctAndDrawn()
    {
        QSet<QString> seen;
        for (int rune = 0; rune < QcRuneGlyphs::count; ++rune) {
            const QString pixels = pixelsOf(rune);
            QVERIFY2(pixels.count(u'#') >= 5, qPrintable(u"rune %1 is nearly empty"_s.arg(rune)));
            QVERIFY2(!seen.contains(pixels), qPrintable(u"rune %1 repeats another"_s.arg(rune)));
            seen.insert(pixels);
        }
        QVERIFY(!QcRuneGlyphs::isSet(QcRuneGlyphs::count, 3, 3));
        QCOMPARE(QcRuneGlyphs::pageCount(), 10);
        QCOMPARE(QcRuneGlyphs::pageName(2), u"qtcraft:runes_2"_s);
        // The floating book's tile: transparent corners, pages in the middle.
        const QImage book = QcRuneGlyphs::bookTile(16);
        QCOMPARE(book.size(), QSize(16, 16));
        QCOMPARE(book.pixelColor(0, 0).alpha(), 0);
        QCOMPARE(book.pixelColor(3, 5).alpha(), 255);

        // A page holds four runes, white, in its 8 x 8 cells; rune 5 is the second of page 1.
        const QImage page = QcRuneGlyphs::page(1, 32);
        QCOMPARE(page.size(), QSize(32, 32));
        const QImage rune = QcRuneGlyphs::image(5, Qt::white);
        for (int row = 0; row < QcRuneGlyphs::height; ++row) {
            for (int column = 0; column < QcRuneGlyphs::width; ++column) {
                QCOMPARE(page.pixelColor(16 + column * 2, row * 2).alpha() > 0, rune.pixelColor(column, row).alpha() > 0);
            }
        }
    }

    void shelvesMakeLettersReadable()
    {
        QCOMPARE(QcRuneScript::readableCount(10, 0), 0);
        QCOMPARE(QcRuneScript::readableCount(10, 7), 4);
        QCOMPARE(QcRuneScript::readableCount(10, 15), 10);
        QCOMPARE(QcRuneScript::readableCount(10, 40), 10);

        const QString clue = u"Fire Protection IV"_s;
        const std::vector<QcRuneScript::Sign> none = QcRuneScript::write(clue, 1234, 0, 0);
        QCOMPARE(static_cast<qsizetype>(none.size()), clue.size());
        QVERIFY(readablePositions(none).isEmpty());
        QCOMPARE(none[4].rune, -1);
        QCOMPARE(none[4].character, u" "_s);
        QCOMPARE(QcRuneScript::legibility(none), (QcRuneScript::Legibility{16, 0}));

        const std::vector<QcRuneScript::Sign> all = QcRuneScript::write(clue, 1234, 0, 15);
        QCOMPARE(QcRuneScript::legibility(all), (QcRuneScript::Legibility{16, 16}));
        QCOMPARE(QcRuneScript::legibility(QcRuneScript::write(clue, 1234, 0, 7)), (QcRuneScript::Legibility{16, 7}));
        QVERIFY(std::ranges::all_of(all, [](const QcRuneScript::Sign &sign) { return sign.rune < 0; }));

        // More shelves only ever add letters, and the same seed gives the same signs.
        QSet<int> before;
        for (int shelves = 0; shelves <= 15; ++shelves) {
            const QSet<int> now = readablePositions(QcRuneScript::write(clue, 1234, 0, shelves));
            QCOMPARE(now.size(), QcRuneScript::readableCount(16, shelves));
            QVERIFY(now.contains(before));
            before = now;
            QCOMPARE(QcRuneScript::write(clue, 1234, 0, shelves), QcRuneScript::write(clue, 1234, 0, shelves));
        }
    }

    void seedAndSlotChooseTheOrder()
    {
        const QString clue = u"Unbreaking III"_s;
        const QSet<int> first = readablePositions(QcRuneScript::write(clue, 1, 0, 7));
        bool otherSeed = false;
        bool otherSlot = false;
        for (quint32 seed = 2; seed < 12; ++seed) {
            otherSeed = otherSeed || readablePositions(QcRuneScript::write(clue, seed, 0, 7)) != first;
        }
        for (int slot = 1; slot < 3; ++slot) {
            otherSlot = otherSlot || readablePositions(QcRuneScript::write(clue, 1, slot, 7)) != first;
        }
        QVERIFY(otherSeed);
        QVERIFY(otherSlot);

        // A substitution: the same hidden letter shows the same rune, in any offer of the seed.
        const std::vector<QcRuneScript::Sign> a = QcRuneScript::write(u"eee"_s, 99, 0, 0);
        const std::vector<QcRuneScript::Sign> b = QcRuneScript::write(u"e"_s, 99, 2, 0);
        QCOMPARE(a[0].rune, a[2].rune);
        QCOMPARE(a[0].rune, b[0].rune);
        QVERIFY(a[0].rune >= 0 && a[0].rune < QcRuneGlyphs::count);
    }

    void pixelArtOfTheScreens()
    {
        const QImage book = QcPixelArtProvider::picture(u"book"_s);
        QCOMPARE(book.size(), QSize(26, 15));
        // The screen's book and the world's share their colours (pages and lines of writing).
        const QImage tile = QcRuneGlyphs::bookTile(16);
        QCOMPARE(book.pixelColor(2, 3), QcRuneGlyphs::bookColour('l'));
        QCOMPARE(tile.pixelColor(1, 4), QcRuneGlyphs::bookColour('l'));
        QCOMPARE(book.pixelColor(2, 2), tile.pixelColor(0, 3));
        const QImage rune = QcPixelArtProvider::picture(u"rune/7/ink"_s);
        QCOMPARE(rune.size(), QSize(QcRuneGlyphs::width, QcRuneGlyphs::height));
        const QColor ink = QcUiPalette::color(QcUiPalette::Role::Ink);
        bool inked = false;
        for (int y = 0; y < rune.height(); ++y) {
            for (int x = 0; x < rune.width(); ++x) {
                inked = inked || rune.pixelColor(x, y) == ink;
            }
        }
        QVERIFY(inked);
        QCOMPARE(QcPixelArtProvider::picture(u"arrow/textSecondary"_s).size(), QSize(14, 11));
        QCOMPARE(QcPixelArtProvider::picture(u"plus/level"_s).size(), QSize(9, 9));
        QCOMPARE(QcPixelArtProvider::picture(u"cross/error"_s).size(), QSize(9, 9));
        QCOMPARE(QcPixelArtProvider::picture(u"spark/level"_s).size(), QSize(7, 7));
        QVERIFY(QcPixelArtProvider::picture(u"rune/40/ink"_s).isNull());
        QVERIFY(QcPixelArtProvider::picture(u"arrow/noSuchRole"_s).isNull());
        QVERIFY(QcPixelArtProvider::picture(u"dragon/ink"_s).isNull());
    }

    void runesFlyFromTheShelvesToTheBook()
    {
        QcEnchantingAura::Table table{{0, 64, 0}, {}};
        for (int x = -2; x <= 2; ++x) {
            table.shelves.append({x, 64, -2});
            table.shelves.append({x, 65, -2});
        }
        QcEnchantingAura aura;
        int seen = 0;
        for (int quarter = 0; quarter < 80; ++quarter) {
            const QList<QcEnchantingAura::Rune> &runes = aura.runesOf(table, quarter * 0.25);
            QVERIFY(runes.size() <= table.shelves.size());
            for (const QcEnchantingAura::Rune &rune : runes) {
                ++seen;
                QVERIFY(rune.rune >= 0 && rune.rune < QcRuneGlyphs::count);
                QVERIFY(rune.size > 0.0 && rune.size <= 0.16);
                // Between the shelves and the table, above the floor.
                QVERIFY(rune.position.z >= -2.0 && rune.position.z <= 1.0);
                QVERIFY(rune.position.y >= 64.0 && rune.position.y <= 67.5);
            }
        }
        QVERIFY(seen > 50);
        QVERIFY(aura.runesOf({{0, 64, 0}, {}}, 3.0).isEmpty());

        // The book opens for a viewer close by and closes for one far away, facing them.
        const QcEnchantingAura::Book near = QcEnchantingAura::bookOf({0, 64, 0}, {0.5, 65.6, 2.5}, 0.0);
        QCOMPARE(near.open, 1.0);
        QVERIFY(std::abs(near.yaw) < 0.2);
        QCOMPARE(QcEnchantingAura::bookOf({0, 64, 0}, {0.5, 65.6, 12.0}, 0.0).open, 0.0);
        QVERIFY(near.spine.y > 65.0 && near.spine.y < 65.3);
    }
};

QTEST_GUILESS_MAIN(TestRuneScript)
#include "test_rune_script.moc"

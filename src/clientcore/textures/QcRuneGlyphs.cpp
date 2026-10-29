// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/textures/QcRuneGlyphs.h"

#include <QPainter>
#include <QPoint>

#include <array>
#include <string_view>

using namespace Qt::StringLiterals;

namespace {

// The size of a page before scaling, as the atlas tiles of the content.
constexpr int baseTile = 16;

using Rune = std::array<std::string_view, QcRuneGlyphs::height>;

// The runes, rows from the top: '#' is set. Their models in Egyptian writing follow the name.
// clang-format off
constexpr std::array<Rune, QcRuneGlyphs::count> runes = {{
    // ankh
    {"..###..", ".#...#.", ".#...#.", "..#.#..", "#######", "...#...", "...#...", "...#..."},
    // eye of Horus
    {".......", ".#####.", "#.###.#", ".#####.", "...#..#", "...#...", "..##...", "......."},
    // water
    {".......", ".......", "#.#.#.#", ".#.#.#.", ".......", "#.#.#.#", ".#.#.#.", "......."},
    // reed leaf
    {"...#...", "..##...", "..###..", "..#.#..", "..###..", "...#...", "...#...", "...#..."},
    // vulture
    {"..##...", ".###...", "..##...", "..###..", ".#####.", "..####.", "..#.#..", ".##.##."},
    // sun disk
    {".......", "..###..", ".#...#.", ".#.#.#.", ".#...#.", "..###..", ".......", "......."},
    // horned viper
    {".......", ".......", "#.#....", ".###...", "...##..", "....###", "......#", "......."},
    // feather of Maat
    {"....##.", "...###.", "...##..", "...##..", "..##...", "..##...", ".##....", ".#....."},
    // scarab
    {"..#.#..", "...#...", ".#####.", "#.###.#", ".#####.", "#.###.#", ".#####.", "..#.#.."},
    // hand
    {".......", ".......", "#####..", "######.", "#######", ".......", ".......", "......."},
    // djed pillar
    {"#######", "..###..", "#######", "..###..", "#######", "..###..", "..###..", ".#####."},
    // was sceptre
    {".##....", "..##...", "...#...", "...#...", "...#...", "...#...", "...#...", "..#.#.."},
    // basket
    {".......", ".......", ".......", "#######", "#.....#", ".#...#.", "..###..", "......."},
    // mouth
    {".......", ".......", "..###..", "##...##", "..###..", ".......", ".......", "......."},
    // house plan
    {".......", "#######", "#.....#", "#.....#", "#..####", "#......", ".......", "......."},
    // owl
    {"..##...", ".#.#...", ".####..", ".#####.", "..####.", "..####.", "..#..#.", ".##.##."},
    // cobra
    {"...##..", "..#..#.", "..#.##.", "...##..", "...#...", "..#....", "..#....", ".####.."},
    // bread loaf
    {".......", ".......", ".......", ".......", "..###..", ".#...#.", "#######", "......."},
    // twisted flax
    {"..###..", ".#...#.", "..###..", ".#...#.", "..###..", ".#...#.", "..###..", "......."},
    // placenta
    {"..###..", ".#####.", "#.#.#.#", "#.#.#.#", "#.#.#.#", ".#####.", "..###..", "......."},
    // door bolt
    {".......", ".......", ".#...#.", "#######", ".#...#.", ".......", ".......", "......."},
    // pool
    {".......", "#######", "#.#.#.#", "##.#.##", "#.#.#.#", "#######", ".......", "......."},
    // hill
    {".......", ".......", ".......", "..###..", ".#####.", "#######", ".......", "......."},
    // jar stand
    {".......", ".#####.", "..#.#..", "..#.#..", "..#.#..", ".#####.", ".......", "......."},
    // tethering rope
    {".......", ".#...#.", "#.#.#.#", ".#...#.", "...#...", "...#...", "...#...", "......."},
    // star
    {"...#...", "...#...", "#######", "..#.#..", ".#...#.", "#.....#", ".......", "......."},
    // crook
    {".###...", "#...#..", "....#..", "....#..", "....#..", "....#..", "....#..", "....#.."},
    // flail
    {"#......", ".#.#.#.", "..#.#.#", "...#.#.", "...#...", "...#...", "...#...", "......."},
    // shen ring
    {"..###..", ".#...#.", ".#...#.", ".#...#.", "..###..", "...#...", ".#####.", "......."},
    // pyramid
    {".......", "...#...", "..###..", ".#.#.#.", "#######", ".......", ".......", "......."},
    // lotus
    {"...#...", "#..#..#", ".#.#.#.", ".#####.", "..###..", "...#...", "...#...", "..###.."},
    // sedge
    {".#.#.#.", ".#.#.#.", "..###..", "...#...", "...#...", "...#...", "..###..", "......."},
    // bee
    {".##.##.", "#.#.#.#", ".#####.", "#.#.#.#", ".#####.", "..###..", "...#...", "......."},
    // ibis
    {".##....", "#.#....", "...#...", "...####", "..#####", "...###.", "...#.#.", "..##.##"},
    // arm
    {".......", ".......", "##.....", ".#.....", ".######", ".....##", ".......", "......."},
    // throw stick
    {".......", "#......", ".#.....", "..##...", "....##.", "......#", ".......", "......."},
    // heart and windpipe
    {"..#.#..", "..###..", ".#####.", ".#####.", "..###..", "...#...", "..###..", "...#..."},
    // water pot
    {"..###..", "...#...", ".#####.", "#.....#", "#.....#", ".#...#.", "..###..", "......."},
    // horizon
    {".......", ".##.##.", "#..#..#", "#.#.#.#", "#..#..#", "#######", ".......", "......."},
    // walking legs
    {".......", ".......", "..###..", "..#.#..", ".#...#.", ".#...#.", "##...##", "......."},
}};

// The floating book: pages (p) with lines of writing (l), the fold (P) and the cover (B, b), in
// bookColour.
constexpr std::array<std::string_view, baseTile> book = {
    "................", "................", ".pppppp..pppppp.", "ppppppPPPPpppppp",
    "plllppPPPPpplllp", "ppppppPPPPpppppp", "pllllpPPPPplllpp", "ppppppPPPPpppppp",
    "plllppPPPPpllllp", "ppppppPPPPpppppp", "BpppppPPPPpppppB", "BBBBBppPPppBBBBB",
    ".BBBBBBBBBBBBBB.", "..bbbbbbbbbbbb..", "................", "................",
};
// clang-format on

} // namespace

namespace QcRuneGlyphs {

bool isSet(int index, int column, int row)
{
    if (index < 0 || index >= count || column < 0 || column >= width || row < 0 || row >= height) {
        return false;
    }
    return runes[static_cast<std::size_t>(index)][static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] == '#';
}

QImage image(int index, const QColor &colour)
{
    QImage picture(width, height, QImage::Format_RGBA8888);
    picture.fill(Qt::transparent);
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            if (isSet(index, column, row)) {
                picture.setPixelColor(column, row, colour);
            }
        }
    }
    return picture;
}

int pageCount()
{
    return (count + runesPerPage - 1) / runesPerPage;
}

QString pageName(int page)
{
    return u"qtcraft:runes_%1"_s.arg(page);
}

int pageOf(int index)
{
    return index / runesPerPage;
}

int cellOf(int index)
{
    return index % runesPerPage;
}

QImage page(int page, int tileSize)
{
    QImage picture(baseTile, baseTile, QImage::Format_RGBA8888);
    picture.fill(Qt::transparent);
    {
        QPainter painter(&picture);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        for (int cell = 0; cell < runesPerPage; ++cell) {
            const int index = page * runesPerPage + cell;
            if (index < count) {
                painter.drawImage(QPoint((cell % cellsPerRow) * cellSize, (cell / cellsPerRow) * cellSize), image(index, Qt::white));
            }
        }
    }
    return tileSize == baseTile ? picture : picture.scaled(tileSize, tileSize, Qt::IgnoreAspectRatio, Qt::FastTransformation);
}

QColor bookColour(char key)
{
    switch (key) {
    case 'p':
        return {0xef, 0xe0, 0xb9};
    case 'P':
        return {0xc9, 0xb3, 0x84};
    case 'l':
        return {0x7a, 0x5a, 0x9a};
    case 'B':
        return {0x8c, 0x33, 0x24};
    case 'b':
        return {0x56, 0x1c, 0x14};
    default:
        return Qt::transparent;
    }
}

QImage bookTile(int tileSize)
{
    QImage picture(baseTile, baseTile, QImage::Format_RGBA8888);
    for (int row = 0; row < baseTile; ++row) {
        for (int column = 0; column < baseTile; ++column) {
            picture.setPixelColor(column, row, bookColour(book[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)]));
        }
    }
    return tileSize == baseTile ? picture : picture.scaled(tileSize, tileSize, Qt::IgnoreAspectRatio, Qt::FastTransformation);
}

} // namespace QcRuneGlyphs

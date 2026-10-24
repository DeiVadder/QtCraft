#pragma once

#include <QColor>
#include <QImage>
#include <QString>

// The runes of the enchanting table (decision E-884): 40 hieroglyphs after Egyptian models (ankh,
// eye, water, reed, vulture, scarab, djed pillar, lotus …), drawn by the game itself like its
// textures and its sign font (QcPixelFont), in the blocky stroke of Pixelify Sans. The enchanting
// screen writes the offers' hidden letters with them (QcRuneScript, served to QML by
// QcPixelArtProvider), and the world draws them as particles flying from the bookshelves to the
// table (QcEnchantingAura) from pages in the texture atlas: four runes in the 2 x 2 cells of 8 x 8
// pixels of a 16 x 16 tile, white on transparent like the font pages.
namespace QcRuneGlyphs {

inline constexpr int count = 40;
inline constexpr int width = 7;
inline constexpr int height = 8;
// Pixels of a rune cell in a page of 16 x 16 pixels, and cells per row and page.
inline constexpr int cellSize = 8;
inline constexpr int cellsPerRow = 2;
inline constexpr int runesPerPage = 4;

// Whether pixel (`column`, `row`) of rune `index` is set; false outside the rune.
[[nodiscard]] bool isSet(int index, int column, int row);
// Rune `index` as a width x height image in `colour` on transparent.
[[nodiscard]] QImage image(int index, const QColor &colour);

[[nodiscard]] int pageCount();
// The atlas name of a page ("qtcraft:runes_<n>"; no content texture can have it).
[[nodiscard]] QString pageName(int page);
// The page and its cell (0..3, row by row) of rune `index`.
[[nodiscard]] int pageOf(int index);
[[nodiscard]] int cellOf(int index);
// Page `page` as a tile of `tileSize` pixels (a multiple of 16), scaled without smoothing.
[[nodiscard]] QImage page(int page, int tileSize);

// The open book floating above the table as an atlas tile (pages with lines of writing, the red
// cover below; the spine in the middle column), and its atlas name.
inline constexpr auto bookTileName = "qtcraft:enchanting_book";
[[nodiscard]] QImage bookTile(int tileSize);
// The colours of both open books, by the key of their pictures: pages (p), lines of writing in the
// runes' violet (l), the fold (P) and the cover (B, b), nothing for any other key. The atlas tile
// and the larger book of the enchanting screen (QcPixelArtProvider, 26 x 15 pixels above the slots)
// have their own shapes, since the tile must fit 16 x 16 and is cut at its spine into the two halves
// that open in the world; they share these colours.
[[nodiscard]] QColor bookColour(char key);

} // namespace QcRuneGlyphs

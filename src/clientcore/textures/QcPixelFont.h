#pragma once

#include <QImage>
#include <QString>

// The 5 x 7 pixel font of the text on signs (decision E-766), drawn by the game itself like its
// textures: printable ASCII, the German umlauts and ß, a few accented letters, € and °; any other
// character shows as '?'. Its glyphs live in pages of the texture atlas (four glyphs in the 2 x 2
// cells of 8 x 8 pixels of a 16 x 16 tile, white on transparent), so sign text is drawn with the
// entity pipeline like everything else. Glyphs are as wide as their pixels (proportional), one pixel
// apart; a space is three pixels wide.
namespace QcPixelFont {

inline constexpr int glyphHeight = 7;
// Pixels of a glyph cell in a page of 16 x 16 pixels, and cells per row and page.
inline constexpr int cellSize = 8;
inline constexpr int cellsPerRow = 2;
inline constexpr int glyphsPerPage = 4;
inline constexpr int spaceWidth = 3;

// Where a glyph is: its page, its cell in the page (0..3, row by row), the first column with pixels
// and the number of columns it takes (0 for a space).
struct Glyph
{
    int page = 0;
    int cell = 0;
    int left = 0;
    int width = 0;
};

// The glyph of a character; '?' for characters the font lacks.
Glyph glyph(char32_t character);
// Pixels a character takes on a line, the gap after it included.
int advance(char32_t character);
// Pixels a line takes, without the gap after its last character.
int lineWidth(const QString &line);
// Whether the font has its own glyph for the character.
bool contains(char32_t character);

int pageCount();
// The atlas name of a page ("qtcraft:glyphs_<n>"; no content texture can have it).
QString pageName(int page);
// Page `page` as a tile of `tileSize` pixels (a multiple of 16), scaled without smoothing.
QImage page(int page, int tileSize);

} // namespace QcPixelFont

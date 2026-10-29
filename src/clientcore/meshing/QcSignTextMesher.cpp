// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/meshing/QcSignTextMesher.h"

#include "clientcore/textures/QcPixelFont.h"

#include <QHash>
#include <QRectF>

#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;

namespace QcSignTextMesher {

namespace {

// Quarter turns of an `attach` value about the vertical axis (north 0, east 1, ...; floor 0).
int quarterTurns(const QString &attach)
{
    if (attach == u"east"_s) {
        return 1;
    }
    if (attach == u"south"_s) {
        return 2;
    }
    return attach == u"west"_s ? 3 : 0;
}

// `p` (cell metres) turned by `angle` (radians) about the middle of the cell, like a shape's spin.
QcVec3 turned(const QcVec3 &p, double angle)
{
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const double x = p.x - 0.5;
    const double z = p.z - 0.5;
    return {0.5 + x * c - z * s, p.y, 0.5 + x * s + z * c};
}

// The rectangle of a glyph in the atlas, inside the rectangle `page` of its page.
QRectF glyphRect(QcPixelFont::Glyph glyph, const QRectF &page)
{
    constexpr double tile = QcPixelFont::cellSize * QcPixelFont::cellsPerRow;
    const double column = (glyph.cell % QcPixelFont::cellsPerRow) * QcPixelFont::cellSize + glyph.left;
    const int row = (glyph.cell / QcPixelFont::cellsPerRow) * QcPixelFont::cellSize;
    return {page.left() + column / tile * page.width(), page.top() + static_cast<double>(row) / tile * page.height(), glyph.width / tile * page.width(),
            QcPixelFont::glyphHeight / tile * page.height()};
}

// One line of text whose top left corner lies `left` and `top` metres from the face's centre.
struct LinePlace
{
    double left = 0.0;
    double top = 0.0;
    // Metres per font pixel along the line.
    double pixel = 0.0;
};

// Writes lines of text on one sign's face as quads, in the sign's light and the ink of its colour.
class Pen
{
public:
    Pen(const Face &face, QcEntityLight light, const QcEntityMesher::UvLookup &uv, QcSignText::Ink ink)
        : m_face(face)
        , m_light(light)
        , m_uv(uv)
        , m_ink{ink.red, ink.green, ink.blue, 255}
    {
    }

    void write(const QString &line, const LinePlace &place)
    {
        double x = place.left;
        const QList<uint> characters = line.toUcs4();
        for (const uint code : characters) {
            const auto character = static_cast<char32_t>(code);
            const QcPixelFont::Glyph glyph = QcPixelFont::glyph(character);
            if (glyph.width > 0) {
                m_quads.push_back(glyphQuad(glyphRect(glyph, pageRect(glyph.page)), place, x, glyph.width));
            }
            x += QcPixelFont::advance(character) * place.pixel;
        }
    }

    [[nodiscard]] std::vector<Quad> takeQuads() { return std::move(m_quads); }

private:
    // The atlas rectangle of font page `page`, looked up once per pen.
    QRectF pageRect(int page)
    {
        const auto known = m_pages.constFind(page);
        if (known != m_pages.cend()) {
            return *known;
        }
        const QRectF rect = m_uv(QcPixelFont::pageName(page));
        m_pages.insert(page, rect);
        return rect;
    }

    // The quad of one glyph `width` font pixels wide whose left edge lies `x` metres along the line.
    Quad glyphQuad(const QRectF &rect, const LinePlace &place, double x, int width) const
    {
        const double x1 = x + width * place.pixel;
        const double bottom = place.top - QcPixelFont::glyphHeight / pixelsPerMetre;
        const std::array<std::pair<double, double>, 4> corners{{{x, bottom}, {x1, bottom}, {x1, place.top}, {x, place.top}}};
        Quad quad;
        for (std::size_t corner = 0; corner < 4; ++corner) {
            const QcVec3 at = m_face.centre + m_face.right * corners[corner].first + QcVec3{0.0, corners[corner].second, 0.0};
            const bool left = corner == 0 || corner == 3;
            quad[corner] = {static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.z), static_cast<float>(left ? rect.left() : rect.right()),
                            static_cast<float>(corner < 2 ? rect.bottom() : rect.top()), 1.0f, m_light.sky, m_light.block, m_ink};
        }
        return quad;
    }

    const Face &m_face;
    QcEntityLight m_light;
    const QcEntityMesher::UvLookup &m_uv;
    // The tint of the glyphs, which are white in the atlas.
    std::array<quint8, 4> m_ink;
    QHash<int, QRectF> m_pages;
    std::vector<Quad> m_quads;
};

} // namespace

std::optional<Face> faceOf(const QcContentRegistry &content, QcBlockId block, QcBlockPos pos)
{
    if (!content.isValidBlock(block) || !content.block(block).sign.isSign()) {
        return std::nullopt;
    }

    // As drawn for rotation 0: a standing board's front at z 0.5625 around y 0.75, a wall board's
    // at z 0.125 around y 0.5, both facing south (+z); a hair in front of the wood.
    const QString attach = content.property(block, u"attach"_s);
    const bool wall = !attach.isEmpty() && attach != u"floor"_s;
    constexpr double lift = 0.004;
    const QcVec3 centre = wall ? QcVec3{0.5, 0.5, 0.125 + lift} : QcVec3{0.5, 0.75, 0.5625 + lift};
    const int sixteenths = quarterTurns(attach) * 4 + std::clamp(content.property(block, u"rotation"_s).toInt(), 0, 15);
    const double angle = sixteenths * std::numbers::pi / 8.0;

    const QcVec3 corner{static_cast<double>(pos.x), static_cast<double>(pos.y), static_cast<double>(pos.z)};
    const QcVec3 middle = turned({0.5, 0.0, 0.5}, angle);
    const QcVec3 east = turned({1.5, 0.0, 0.5}, angle) - middle;
    const QcVec3 south = turned({0.5, 0.0, 1.5}, angle) - middle;
    return Face{corner + turned(centre, angle), east, south};
}

std::vector<Quad> textQuads(const Face &face, const QcSignText::Text &text, QcEntityLight light, const QcEntityMesher::UvLookup &uv)
{
    Pen pen(face, light, uv, QcSignText::inkOf(text.color));
    const QcSignText::Lines &lines = text.lines;
    const double pixel = 1.0 / pixelsPerMetre;
    const double blockHeight = (QcSignText::maxLines * lineStep - (lineStep - QcPixelFont::glyphHeight)) * pixel;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const int width = QcPixelFont::lineWidth(lines[index]);
        if (width == 0) {
            continue;
        }
        // Too wide for the board: narrower glyphs, as high as the others.
        const double along = width > maxLineWidth ? pixel * maxLineWidth / width : pixel;
        const double top = blockHeight / 2.0 - static_cast<double>(index) * lineStep * pixel;
        pen.write(lines[index], {-width * along / 2.0, top, along});
    }
    return pen.takeQuads();
}

} // namespace QcSignTextMesher

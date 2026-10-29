// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/models/QcPixelArtProvider.h"

#include "app/debug/QcAppLogging.h"
#include "clientcore/settings/QcUiPalette.h"
#include "clientcore/textures/QcRuneGlyphs.h"

#include <QStringList>

#include <array>
#include <optional>
#include <span>
#include <string_view>

using namespace Qt::StringLiterals;

namespace {

// A picture as rows of characters: '.' is transparent, every other character a colour of its
// palette.
struct Art
{
    std::string_view name;
    std::span<const std::string_view> rows;
};

// clang-format off
constexpr std::array<std::string_view, 11> arrow = {
    "........#.....", "........##....", "........###...", "############..", "#############.", "##############",
    "#############.", "############..", "........###...", "........##....", "........#.....",
};
constexpr std::array<std::string_view, 9> plus = {
    "...###...", "...###...", "...###...", "#########", "#########", "#########", "...###...", "...###...", "...###...",
};
constexpr std::array<std::string_view, 9> cross = {
    "##.....##", "###...###", ".###.###.", "..#####..", "...###...", "..#####..", ".###.###.", "###...###", "##.....##",
};
constexpr std::array<std::string_view, 7> spark = {
    "...#...", "...#...", "..###..", "#######", "..###..", "...#...", "...#...",
};
// The open book: pages (p) with lines of writing (l), the fold's shadow (P) and the cover (B, b), in
// the colours of the world's book (QcRuneGlyphs::bookColour, which says why the shapes differ).
constexpr std::array<std::string_view, 15> book = {
    "..........................",
    "..pppppppp......pppppppp..",
    ".pppppppppppPPppppppppppp.",
    ".plllllpppppPPpppplllllpp.",
    ".ppppppppppPPPPpppppppppp.",
    ".plllllllppPPPPpplllllllp.",
    ".ppppppppppPPPPpppppppppp.",
    ".pllllllpppPPPPppplllllpp.",
    ".ppppppppppPPPPpppppppppp.",
    ".plllllllppPPPPpplllllllp.",
    ".ppppppppppPPPPpppppppppp.",
    "BBpppppppppPPPPpppppppppBB",
    "BBBBBpppppppPPpppppppBBBBB",
    ".BBBBBBBBBBBBBBBBBBBBBBBB.",
    "..bbbbbbbbbbbbbbbbbbbbbb..",
};
// clang-format on

constexpr std::array<Art, 4> symbols = {{{"arrow", arrow}, {"plus", plus}, {"cross", cross}, {"spark", spark}}};

// `rows` drawn with `colourOf` for each character.
template <typename ColourOf>
QImage paint(std::span<const std::string_view> rows, ColourOf colourOf)
{
    QImage image(static_cast<int>(rows.front().size()), static_cast<int>(rows.size()), QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    for (std::size_t row = 0; row < rows.size(); ++row) {
        for (std::size_t column = 0; column < rows[row].size(); ++column) {
            if (rows[row][column] != '.') {
                image.setPixelColor(static_cast<int>(column), static_cast<int>(row), colourOf(rows[row][column]));
            }
        }
    }
    return image;
}

// The colour of the palette whose QML name is `name`.
std::optional<QColor> roleColour(QStringView name)
{
    for (const QcUiPalette::Entry &entry : QcUiPalette::entries()) {
        if (name == QLatin1StringView(entry.name)) {
            return QColor::fromRgba(entry.argb);
        }
    }
    return std::nullopt;
}

// A one-colour picture: a rune or a symbol, in the colour of a role.
QImage symbol(const QStringList &parts)
{
    const std::optional<QColor> colour = roleColour(parts.constLast());
    if (!colour) {
        return {};
    }
    if (parts.constFirst() == u"rune"_s && parts.size() == 3) {
        bool ok = false;
        const int index = parts.at(1).toInt(&ok);
        return ok && index >= 0 && index < QcRuneGlyphs::count ? QcRuneGlyphs::image(index, *colour) : QImage();
    }
    for (const Art &art : symbols) {
        if (parts.size() == 2 && parts.constFirst() == QLatin1StringView(art.name.data(), static_cast<qsizetype>(art.name.size()))) {
            return paint(art.rows, [&colour](char) { return *colour; });
        }
    }
    return {};
}

} // namespace

QcPixelArtProvider::QcPixelArtProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage QcPixelArtProvider::picture(const QString &id)
{
    if (id == u"book"_s) {
        return paint(book, QcRuneGlyphs::bookColour);
    }
    const QStringList parts = id.split(u'/');
    return parts.size() >= 2 ? symbol(parts) : QImage();
}

QImage QcPixelArtProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    // Pictures come at their pixel size; QML scales them without smoothing.
    Q_UNUSED(requestedSize)
    const QImage image = picture(id);
    if (image.isNull()) {
        qCWarning(lcGame) << "Pixel art: unknown picture" << id;
    }
    if (size) {
        *size = image.size();
    }
    return image;
}

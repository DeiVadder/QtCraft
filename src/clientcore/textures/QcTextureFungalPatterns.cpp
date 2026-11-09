// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/textures/QcTextureFungalPatterns.h"

#include "clientcore/textures/QcTextureField.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace {

using namespace QcTextureField;
using Pattern = QcTextureSpec::Pattern;
using QcTexturePaint::scaled;
using QcTextureRelief::Entry;
using QcTextureRelief::Surface;

constexpr double tile = 16.0;
constexpr double tau = 2.0 * std::numbers::pi;

// A ridge running along a wave with whole periods on the tile: 1 on the crest, 0 in the furrow.
double ridge(double waves)
{
    return std::abs(std::sin(std::numbers::pi * waves / tile));
}

// Felt: two layers of threads crossing at angles of their own, with fleshy knobs on top. The
// threads are a soft, shallow weave with light furrows, so a floor of felt reads calm from a
// distance and the knobs carry the pattern.
Surface paintFungalFelt(const QcTextureSpec &spec, QPointF point)
{
    const QPointF bent = warped(spec.seed + 1, point, 8.0, 2.8);
    const double first = ridge(5.0 * bent.x() + 3.0 * bent.y());
    const double second = ridge(3.0 * bent.x() - 6.0 * bent.y());
    const double threads = std::max(first, second);

    const Cell knob = cellAt(spec.seed + 2, warped(spec.seed + 3, point, 4.0, 0.8), {5, 5, 0.95, false});
    const double raised = knob.id % 3 == 0 ? 1.0 - smooth(0.2, 1.3, knob.distance) : 0.0;
    const double cloud = fractal(spec.seed + 4, point, 8.0, 2);

    const double height = 0.4 * threads + 1.6 * raised + 0.6 * cloud;
    const QColor flesh = scaled(spec.primary, 1.0 + 0.22 * raised + 0.1 * cloud);
    return {height, mixed(flesh, spec.secondary, 0.45 * smooth(0.75, 0.2, threads) * (1.0 - raised))};
}

// Cushions: round pads of fine tufts, of many sizes, close together.
Surface paintMossCushions(const QcTextureSpec &spec, QPointF point)
{
    const Cell pad = cellAt(spec.seed + 1, warped(spec.seed + 2, point, 8.0, 1.6), {4, 4, 0.95, false});
    const Cell small = cellAt(spec.seed + 3, point, {8, 8, 0.9, false});
    const double dome = smooth(0.0, 1.8, pad.edge);
    const double bump = smooth(0.0, 0.7, small.edge);
    const double tuft = fractal(spec.seed + 4, point, 2.0, 2);

    const double height = 2.3 * dome + 0.6 * bump + 0.5 * tuft;
    const double tone = 0.86 + 0.3 * unit(pad.id) + 0.22 * tuft;
    return {height, mixed(scaled(spec.primary, tone), spec.secondary, 1.0 - smooth(0.05, 0.75, pad.edge))};
}

// The edge of a mat over rock: threads hang over the side in uneven tongues.
Surface paintMatEdge(const QcTextureSpec &spec, QPointF point)
{
    const double y = point.y();
    const QPointF along{point.x(), 0.0};
    const double reach = 4.0 + 3.0 * noise(spec.seed + 1, along, 4.0) + 3.4 * noise(spec.seed + 2, along, 2.0);

    const Cell lump = cellAt(spec.seed + 3, warped(spec.seed + 4, point, 8.0, 2.2), {4, 4, 0.95, false});
    const double seam = 1.0 - smooth(0.1, 0.7, lump.edge);
    const Surface rock{1.3 * smooth(0.0, 2.2, lump.edge), mixed(scaled(spec.secondary, 0.9 + 0.3 * unit(lump.id)), scaled(spec.secondary, 0.45), seam)};

    const double inside = reach - y;
    if (inside <= 0.0) {
        // The shadow the mat throws on the rock.
        return {rock.height, scaled(rock.colour, 0.7 + 0.3 * smooth(0.0, 1.4, -inside))};
    }
    const double tuft = fractal(spec.seed + 5, point, 2.0, 2);
    const double strand = ridge(7.0 * point.x() + 0.9 * y);
    return {3.0 + 1.0 * smooth(0.0, 1.0, inside) + 0.7 * strand + 0.5 * tuft, scaled(spec.primary, 0.92 + 0.3 * strand + 0.3 * tuft)};
}

// Ropes of fibre twisted around each other, leaning a little.
Surface paintRopeStem(const QcTextureSpec &spec, QPointF point)
{
    const QPointF bent = warped(spec.seed + 1, point, 8.0, 1.5);
    const double rope = ridge(4.0 * bent.x() + bent.y());
    const double twist = 0.5 + 0.5 * std::sin(tau * (6.0 * bent.y() - 2.0 * bent.x()) / tile);
    const double grain = noise(spec.seed + 2, point, QSizeF(2.0, 8.0));

    const double height = 2.6 * std::sqrt(rope) + 0.55 * twist * rope + 0.4 * grain;
    const QColor fibre = scaled(spec.primary, 0.88 + 0.26 * twist + 0.3 * grain);
    return {height, mixed(fibre, spec.secondary, 1.0 - smooth(0.05, 0.5, rope))};
}

// Upright ribs of dark flesh, scattered with pores that shine with what runs inside.
Surface paintPoredStem(const QcTextureSpec &spec, QPointF point)
{
    const QPointF bent = warped(spec.seed + 1, point, 16.0, 1.3);
    const double rib = ridge(3.0 * bent.x());
    const Cell pore = cellAt(spec.seed + 2, point, {5, 7, 0.9, false});
    const double radius = 0.45 + 0.6 * unit(pore.id >> 8);
    const double hole = pore.id % 5 < 2 ? 1.0 - smooth(radius * 0.5, radius, pore.distance) : 0.0;
    const double streak = noise(spec.seed + 3, point, QSizeF(2.0, 16.0));

    const double height = 2.2 * smooth(0.0, 0.7, rib) + 0.5 * streak - 1.8 * hole;
    const QColor flesh = scaled(spec.primary, 0.82 + 0.34 * rib + 0.3 * streak);
    return {height, mixed(flesh, spec.secondary, std::max(hole, 0.5 * (1.0 - smooth(0.0, 0.3, rib))))};
}

// The cut through a stem: bundled tubes, walls (primary) around their hollows (secondary).
Surface paintStemSection(const QcTextureSpec &spec, QPointF point)
{
    const Cell tube = cellAt(spec.seed + 1, warped(spec.seed + 2, point, 8.0, 0.9), {4, 4, 0.8, true});
    const double size = 0.7 + 0.8 * unit(tube.id);
    const double hollow = 1.0 - smooth(size * 0.5, size, tube.distance);
    const double wall = smooth(0.0, 0.5, tube.edge);
    const double height = 1.3 * wall - 1.6 * hollow;
    return {height, mixed(scaled(spec.primary, 0.85 + 0.3 * unit(tube.id >> 8) + 0.1 * wall), spec.secondary, hollow)};
}

// Bare fibre with the skin peeled off: long wavering strands with a few dark flecks.
Surface paintPeeledFibre(const QcTextureSpec &spec, QPointF point)
{
    const QPointF bent = warped(spec.seed + 1, point, 16.0, 1.1);
    const double strand = noise(spec.seed + 2, bent, QSizeF(1.0, 16.0));
    const double wide = noise(spec.seed + 3, bent, QSizeF(4.0, 16.0));
    const Cell fleck = cellAt(spec.seed + 4, point, {4, 3, 0.9, false});
    const double mark = fleck.id % 4 == 0 ? 1.0 - smooth(0.0, 0.5, std::abs(point.x() - fleck.centre.x()) + 0.28 * std::abs(point.y() - fleck.centre.y())) : 0.0;

    const double height = 1.1 * strand + 0.6 * wide - 0.7 * mark;
    return {height, mixed(scaled(spec.primary, 1.0 + 0.32 * strand + 0.2 * wide), spec.secondary, 0.9 * mark)};
}

// Long boards across the tile, each with its own fibres and a butt joint of its own.
Surface paintFibreBoards(const QcTextureSpec &spec, QPointF point)
{
    static constexpr int boards = 4;
    // The rows start off the tile's edge, so the tile ends inside a board.
    const double y = std::fmod(point.y() + 1.5, tile);
    const int board = static_cast<int>(y / (tile / boards)) % boards;
    const quint32 own = spec.seed + 10 * static_cast<quint32>(board);
    const double inBoard = y - board * (tile / boards);
    const double joint = 2.0 + 12.0 * unit(QcTexturePaint::hashPixel(own, 1, 2));
    const double fromJoint = std::abs(std::remainder(point.x() - joint, tile));

    const QPointF bent = warped(own + 1, point, 8.0, 0.9);
    const double fibre = noise(own + 2, bent, QSizeF(8.0, 1.0));
    const double seam = std::max(1.0 - smooth(0.0, 0.55, std::min(inBoard, tile / boards - inBoard)), 1.0 - smooth(0.05, 0.4, fromJoint));

    const double height = 1.5 * (1.0 - seam) + 0.55 * fibre;
    const double tone = 0.9 + 0.2 * unit(QcTexturePaint::hashPixel(own, 3, 4)) + 0.34 * fibre;
    return {height, mixed(scaled(spec.primary, tone), spec.secondary, seam)};
}

// Upright boards of unequal width, the fibre full of small pits.
Surface paintPittedBoards(const QcTextureSpec &spec, QPointF point)
{
    // The first board runs over the tile's edge.
    static constexpr std::array<double, 4> seams{2.5, 7.5, 13.5, tile + 2.5};
    const double x = point.x() < seams[0] ? point.x() + tile : point.x();
    const int board = x < seams[1] ? 0 : (x < seams[2] ? 1 : 2);
    const double toSeam = std::min(x - seams[static_cast<std::size_t>(board)], seams[static_cast<std::size_t>(board) + 1] - x);
    const double seam = 1.0 - smooth(0.0, 0.5, toSeam);

    const Cell pit = cellAt(spec.seed + 1 + static_cast<quint32>(board), point, {7, 7, 0.95, false});
    const double hole = pit.id % 3 == 0 ? 1.0 - smooth(0.12, 0.55, pit.distance) : 0.0;
    const double flame = noise(spec.seed + 5, warped(spec.seed + 6, point, 8.0, 1.0), QSizeF(2.0, 8.0));

    const double height = 1.4 * (1.0 - seam) + 0.45 * flame - 0.9 * hole;
    const double tone = 0.92 + 0.14 * board * 0.5 + 0.3 * flame;
    return {height, mixed(scaled(spec.primary, tone), spec.secondary, std::max(seam, 0.85 * hole))};
}

// Flesh swollen in lumps, on them smaller warts.
Surface paintWartLumps(const QcTextureSpec &spec, QPointF point)
{
    const Cell lump = cellAt(spec.seed + 1, warped(spec.seed + 2, point, 8.0, 1.7), {4, 4, 0.95, false});
    const Cell wart = cellAt(spec.seed + 3, point, {9, 9, 0.9, false});
    const double dome = std::sqrt(smooth(0.0, 2.0, lump.edge));
    const double pimple = 1.0 - smooth(0.1, 0.7, wart.distance);
    const double height = 3.0 * dome + 0.8 * pimple * dome;
    const double tone = 0.86 + 0.26 * unit(lump.id) + 0.22 * pimple;
    return {height, mixed(scaled(spec.primary, tone), spec.secondary, 1.0 - smooth(0.05, 0.6, lump.edge))};
}

// Flesh in winding folds, like the contour lines of its own swelling.
Surface paintWartFolds(const QcTextureSpec &spec, QPointF point)
{
    const double swelling = fractal(spec.seed + 1, point, 4.0, 2);
    const double fold = std::abs(std::sin(7.0 * swelling + 2.0 * noise(spec.seed + 2, point, 2.0)));
    const double speck = noise(spec.seed + 3, point, 2.0);
    const double height = 2.4 * smooth(0.0, 0.8, fold) + 0.3 * speck;
    return {height, mixed(scaled(spec.primary, 0.84 + 0.28 * fold + 0.2 * speck), spec.secondary, 1.0 - smooth(0.04, 0.42, fold))};
}

// Glowing lobes in a thin skin: light has no relief, so only the skin is shaded.
Surface paintGlowLobes(const QcTextureSpec &spec, QPointF point)
{
    const Cell lobe = cellAt(spec.seed + 1, warped(spec.seed + 2, point, 8.0, 1.1), {4, 4, 0.9, true});
    const double core = 1.0 - smooth(0.0, 3.2, lobe.distance);
    const double vein = 1.0 - smooth(0.03, 0.3, lobe.edge);
    const double warmth = 0.45 + 0.6 * core * (0.7 + 0.3 * unit(lobe.id)) + 0.14 * fractal(spec.seed + 3, point, 4.0, 2);
    const QColor glow = mixed(spec.primary, QColor(255, 244, 200), smooth(0.55, 1.05, warmth));
    return {0.5 * (1.0 - vein), mixed(glow, spec.secondary, vein)};
}

constexpr std::array<Entry, 12> surfaces = {{
    {Pattern::FungalFelt, paintFungalFelt},
    {Pattern::MossCushions, paintMossCushions},
    {Pattern::MatEdge, paintMatEdge},
    {Pattern::RopeStem, paintRopeStem},
    {Pattern::PoredStem, paintPoredStem},
    {Pattern::StemSection, paintStemSection},
    {Pattern::PeeledFibre, paintPeeledFibre},
    {Pattern::FibreBoards, paintFibreBoards},
    {Pattern::PittedBoards, paintPittedBoards},
    {Pattern::WartLumps, paintWartLumps},
    {Pattern::WartFolds, paintWartFolds},
    {Pattern::GlowLobes, paintGlowLobes},
}};

} // namespace

std::span<const Entry> qcFungalSurfaces()
{
    return surfaces;
}

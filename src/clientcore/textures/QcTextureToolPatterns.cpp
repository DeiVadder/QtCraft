// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/textures/QcTextureToolPatterns.h"

#include <array>
#include <utility>

namespace {

using namespace QcTexturePaint;

// The tools lie on the diagonal from the bottom left to the top right: `line` (x + y) is
// constant along it, size - 1 on the handle, and `along` (x - y) grows towards the top right.
int lineOf(int x, int y)
{
    return x + y;
}

int alongOf(int x, int y)
{
    return x - y;
}

// Brightness of a metal part: only a trace of the pixel variation, so a blade or a head reads as
// one piece when enlarged.
double calm(const QcTextureSpec &spec, int x, int y)
{
    return 1.0 + 0.3 * spec.variation * noise(spec.seed, x, y);
}

// A run of an axe's head along one line: the values of `along` it covers.
struct Run
{
    int low = 0;
    int high = 0;
};

// The axe's head line by line from the cutting edge (line 6) to the cheek at the haft (line 14):
// the bit is widest just behind the edge, its lower horn reaching further down the haft (the
// beard), and narrows towards the haft.
constexpr int firstHeadLine = 6;
constexpr std::array<Run, 9> headRuns = {{{-2, 4}, {-4, 5}, {-4, 4}, {-6, 6}, {-4, 5}, {-3, 4}, {-1, 3}, {0, 2}, {0, 2}}};
// The haft passes through the eye, these values of `along`; the poll sits behind it.
constexpr Run eye{0, 2};

bool inHead(int line, int along)
{
    const int index = line - firstHeadLine;
    if (index < 0 || std::cmp_greater_equal(index, headRuns.size())) {
        return false;
    }
    const Run run = headRuns.at(static_cast<std::size_t>(index));
    return along >= run.low && along <= run.high;
}

bool inEye(int along)
{
    return along >= eye.low && along <= eye.high;
}

} // namespace

QColor qcPaintDetailedSword(const QcTextureSpec &spec, int x, int y, QColor)
{
    // `line` is size - 1 on the blade's upper edge and size on its lower one.
    const int line = lineOf(x, y);
    const int along = alongOf(x, y);
    const bool blade = (line == size - 1 && along >= -3 && along <= 13) || (line == size && along >= -2 && along <= 10);
    if (blade) {
        return scaled(spec.primary, calm(spec, x, y));
    }

    const bool guard = along == -4 && line >= size - 4 && line <= size + 2;
    const bool pommel = along <= -14 && line >= size - 2 && line <= size;
    if (guard || pommel) {
        return scaled(spec.primary, 0.6 * calm(spec, x, y));
    }
    if (line == size - 1 && along >= -13 && along <= -5) {
        // The wrap of the grip: every other pixel darker.
        const QColor wrap = ((along + 13) / 2) % 2 == 0 ? spec.secondary : scaled(spec.secondary, 0.8);
        return varied(spec, wrap, x, y, 5);
    }
    return clear();
}

QColor qcPaintDetailedAxe(const QcTextureSpec &spec, int x, int y, QColor)
{
    const int line = lineOf(x, y);
    const int along = alongOf(x, y);
    if (line == size - 1 && along >= -11 && along <= 5) {
        // The haft, in the shadow of the head where it passes through the eye.
        const QColor wood = varied(spec, spec.secondary, x, y, 5);
        return inEye(along) ? scaled(wood, 0.7) : wood;
    }
    if ((line == size || line == size + 1) && inEye(along)) {
        return scaled(spec.primary, 0.8 * calm(spec, x, y));
    }
    if (!inHead(line, along)) {
        return clear();
    }

    // The cutting edge: the pixels with no head beyond them towards the top left.
    if (!inHead(line - 2, along)) {
        return scaled(spec.primary, 1.25 * calm(spec, x, y));
    }
    // The cheek at the haft, darker than the bit.
    if (line >= size - 3) {
        return scaled(spec.primary, 0.65 * calm(spec, x, y));
    }
    return scaled(spec.primary, 0.92 * calm(spec, x, y));
}

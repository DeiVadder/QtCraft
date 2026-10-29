// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// The frame choice of block.vert (decision E-878) ported to float32 the way GPUs may compute it:
// a / b as a * rcp(b) (Metal fast math, Direct3D), with a reciprocal that is correctly rounded or
// one unit in the last place off, with and without fused multiply-add. Every tile index of every
// atlas width from 1 to 128 columns lands in its own cell, and every frame of every animation is
// picked as QcTextureAnimation::frameAt picks it. Also the clock that drives the time.
#include "clientcore/textures/QcAnimationClock.h"
#include "common/content/blocks/QcTextureAnimation.h"

#include <QTest>

#include <algorithm>
#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// How a GPU may divide and multiply-add.
struct Gpu
{
    // Units in the last place the reciprocal is off (-1, 0, 1).
    int rcpUlps = 0;
    // Whether x - y * z is one fused operation.
    bool fused = false;

    float rcp(float b) const
    {
        float r = 1.0f / b;
        for (int i = 0; i < std::abs(rcpUlps); ++i) {
            r = std::nextafter(r, rcpUlps > 0 ? 2.0f * r : 0.0f);
        }
        return r;
    }
    float divide(float a, float b) const { return a * rcp(b); }
    // x - y * z.
    float minusProduct(float x, float y, float z) const
    {
        if (fused) {
            return std::fma(-y, z, x);
        }
        const float product = y * z;
        return x - product;
    }
    // GLSL mod(): x - y * floor(x / y).
    float mod(float x, float y) const { return minusProduct(x, y, std::floor(divide(x, y))); }
};

constexpr std::array<Gpu, 6> gpus = {{{0, false}, {1, false}, {-1, false}, {0, true}, {1, true}, {-1, true}}};

QString describe(Gpu gpu)
{
    return u"rcp %1 ulp%2"_s.arg(gpu.rcpUlps).arg(gpu.fused ? u", fused"_s : QString());
}

constexpr float wholeBias = 1e-4f;

// The cell of tile `index` in a grid `columns` wide, computed with integers.
std::array<float, 2> exactCell(int index, int columns)
{
    const int row = index / columns;
    return {static_cast<float>(index % columns), static_cast<float>(row)};
}

// block.vert inTile(): the cell of tile `index` in a grid `columns` wide.
std::array<float, 2> tileCell(Gpu gpu, float index, float columns)
{
    const float row = std::floor(gpu.divide(index + 0.5f, columns));
    return {gpu.minusProduct(index, row, columns), row};
}

// The cell inTile() computed before: mod() and floor() of index / columns.
std::array<float, 2> oldTileCell(Gpu gpu, float index, float columns)
{
    return {gpu.mod(index, columns), std::floor(gpu.divide(index, columns))};
}

// The atlas cell a coordinate near the edge `inside` (0..1) of the cell `first` lands in when
// inTile() moves it into the cell `cell` of a grid `columns` x `rows`.
std::array<float, 2> landing(Gpu gpu, std::array<float, 2> first, std::array<float, 2> cell, float inside, std::array<float, 2> grid)
{
    std::array<float, 2> landed{};
    for (std::size_t axis = 0; axis < 2; ++axis) {
        const float uv = (first[axis] + inside) * gpu.rcp(grid[axis]);
        const float moved = uv + (cell[axis] - first[axis]) * gpu.rcp(grid[axis]);
        landed[axis] = std::floor(moved * grid[axis]);
    }
    return landed;
}

// block.vert animate(): frame, next frame and blend at `time`.
QcTextureAnimation::Frame shaderFrame(Gpu gpu, float time, int frames, int frameTicks)
{
    const auto count = static_cast<float>(frames);
    const auto ticks = static_cast<float>(frameTicks);
    const float position = gpu.divide(gpu.mod(time, count * ticks), ticks);
    float index = std::floor(position + wholeBias);
    index += count * ((index <= -0.5f ? 1.0f : 0.0f) - (index >= count ? 1.0f : 0.0f));
    float next = index + 1.0f;
    next -= next >= count ? count : 0.0f;
    const float blend = std::clamp(position - std::floor(position + wholeBias), 0.0f, 1.0f);
    return {static_cast<int>(index), static_cast<int>(next), blend};
}

// The first wrong tile of a grid `columns` wide on `gpu`, -1 for none.
int firstWrongTile(Gpu gpu, int columns)
{
    const int rows = columns + 1;
    const std::array<float, 2> grid = {static_cast<float>(columns), static_cast<float>(rows)};
    const int first = columns * rows / 3;
    const std::array<float, 2> firstCell = exactCell(first, columns);
    for (int index = 0; index < columns * rows; ++index) {
        const std::array<float, 2> cell = tileCell(gpu, static_cast<float>(index), grid[0]);
        const std::array<float, 2> expected = exactCell(index, columns);
        for (const float inside : {0.12f, 0.88f}) {
            if (landing(gpu, firstCell, cell, inside, grid) != expected) {
                return index;
            }
        }
    }
    return -1;
}

// Whether frame selection on `gpu` differs from frameAt() for `animation` anywhere tested; the
// problem, or an empty string.
QString frameMismatch(Gpu gpu, QcTextureAnimation animation)
{
    const int cycle = animation.cycleTicks();
    for (const int loops : {0, 1, (1 << 20) / cycle - 1}) {
        for (int frame = 0; frame < animation.frames; ++frame) {
            for (const double share : {0.0, 0.5, 0.875}) {
                const double time = static_cast<double>(loops) * cycle + (frame + share) * animation.frameTicks;
                const QcTextureAnimation::Frame expected = animation.frameAt(time);
                const QcTextureAnimation::Frame shown = shaderFrame(gpu, static_cast<float>(time), animation.frames, animation.frameTicks);
                if (shown.index != expected.index || shown.next != expected.next || std::abs(shown.blend - expected.blend) > 1e-3) {
                    return u"at %1: frame %2 blend %3, expected %4 blend %5"_s.arg(time).arg(shown.index).arg(shown.blend).arg(expected.index).arg(expected.blend);
                }
            }
        }
    }
    return {};
}

} // namespace

class TestTextureAnimationShader : public QObject
{
    Q_OBJECT

private slots:
    // Every tile of every grid from 1 to 128 columns, with a coordinate near either edge of the
    // first frame's tile moved into it, ends in the tile's cell.
    void everyTileLandsInItsCell()
    {
        for (const Gpu gpu : gpus) {
            for (int columns = 1; columns <= 128; ++columns) {
                const int wrong = firstWrongTile(gpu, columns);
                QVERIFY2(wrong < 0, qPrintable(u"%1: %2 columns, tile %3"_s.arg(describe(gpu)).arg(columns).arg(wrong)));
            }
        }
    }

    // The port reproduces what the review found: floor(index / columns) with a reciprocal goes
    // wrong for some widths, so the test above would see a regression.
    void oldTileFormulaFailsSomewhere()
    {
        int failing = 0;
        for (const Gpu gpu : gpus) {
            for (int columns = 1; columns <= 128; ++columns) {
                for (int index = 0; index < columns * (columns + 1); ++index) {
                    const std::array<float, 2> cell = oldTileCell(gpu, static_cast<float>(index), static_cast<float>(columns));
                    if (cell != exactCell(index, columns)) {
                        ++failing;
                        break;
                    }
                }
            }
        }
        qInfo() << "Grid widths and GPU models the old formula gets wrong:" << failing;
        QVERIFY(failing > 0);
    }

    // Every frame of animations over the whole range of the definitions, at its first tick, in its
    // middle and late in it, early and late in the wrapped time (up to QcTextureAtlas::maxCycleTicks),
    // is picked as frameAt() picks it.
    void framesArePickedAsOnTheCpu()
    {
        for (const Gpu gpu : gpus) {
            for (int frames = 2; frames <= QcTextureAnimation::maxFrames; ++frames) {
                for (int frameTicks = 1; frameTicks <= QcTextureAnimation::maxFrameTicks; frameTicks += (frameTicks < 8 ? 1 : 7)) {
                    const QcTextureAnimation animation{.frames = frames, .frameTicks = frameTicks, .interpolate = true};
                    const QString problem = frameMismatch(gpu, animation);
                    QVERIFY2(problem.isEmpty(), qPrintable(u"%1: %2 frames of %3 ticks %4"_s.arg(describe(gpu)).arg(frames).arg(frameTicks).arg(problem)));
                }
            }
        }
    }

    // The clock runs at the server's speed and stands at 0 (pause, /tick freeze); it goes on from
    // where it stood.
    void clockFollowsTheServerSpeed()
    {
        QcAnimationClock clock;
        QCOMPARE(clock.ticks(), 0.0);
        clock.advance(0.5, 1.0);
        QCOMPARE(clock.ticks(), 10.0);
        clock.advance(3.0, 0.0);
        QCOMPARE(clock.ticks(), 10.0);
        clock.advance(0.05, 1.0);
        QCOMPARE(clock.ticks(), 11.0);
        // A sprint at four times the rate.
        clock.advance(0.25, 4.0);
        QCOMPARE(clock.ticks(), 31.0);
        // Nothing runs backwards.
        clock.advance(-1.0, 1.0);
        clock.advance(1.0, -1.0);
        QCOMPARE(clock.ticks(), 31.0);
    }
};

QTEST_GUILESS_MAIN(TestTextureAnimationShader)
#include "test_texture_animation_shader.moc"

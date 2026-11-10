// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/blending/QcRingBlender.h"

#include "worldgen/blending/QcBlendedChunk.h"
#include "worldgen/blending/QcBlendGrid.h"
#include "worldgen/generators/QcWorldGenerator.h"

#include <QThread>
#include <QThreadPool>

#include <algorithm>
#include <atomic>
#include <vector>

namespace {

using QcRingBlender::Blended;
using QcRingBlender::Job;

// A ring takes this many rounds to settle around its structures at most; after them it stays as
// it is, and a structure found last has only the part of its edge that fits.
constexpr int planRounds = 6;
// The threads of a conversion.
constexpr int mostThreads = 8;
// How long the converting thread waits between two reports, in milliseconds.
constexpr int reportEvery = 50;

// How a pass over the ring runs: its threads, and the steps it reports to its control, from
// `firstStep` of `totalSteps`.
struct Reporting
{
    const QcConversionControl &control;
    int threads = 0;
    qint64 firstStep = 0;
    qint64 totalSteps = 0;
};

// Runs `work` for each index below `count` on several threads, the converting thread reporting.
// Returns false when the control stopped it.
bool runAll(qsizetype count, const std::function<void(qsizetype)> &work, const Reporting &reporting)
{
    std::atomic<qsizetype> next{0};
    std::atomic<qsizetype> done{0};
    std::atomic<bool> stop{false};
    QThreadPool pool;
    const int threads = reporting.threads > 0 ? reporting.threads : std::clamp(QThread::idealThreadCount(), 1, mostThreads);
    pool.setMaxThreadCount(threads);
    for (int i = 0; i < threads; ++i) {
        pool.start([&] {
            for (qsizetype index = next++; index < count && !stop.load(); index = next++) {
                work(index);
                ++done;
            }
        });
    }
    while (!pool.waitForDone(reportEvery)) {
        if (reporting.control.stopped()) {
            stop = true;
        }
        reporting.control.report({QcConversionControl::Stage::Blend, reporting.firstStep + done.load(), reporting.totalSteps});
    }
    return !stop.load() && !reporting.control.stopped();
}

// The shapes of the chunks `ring` in both versions; none when `control` stops it.
std::optional<QcBlendSources> generateShapes(const Job &job, const QList<QcChunkPos> &ring, const QcConversionControl &control)
{
    std::vector<QcBlendSource> sources(static_cast<std::size_t>(ring.size()));
    const auto work = [&](qsizetype index) {
        const QcChunkPos pos = ring.at(index);
        sources[static_cast<std::size_t>(index)] = {std::make_shared<const QcChunkShape>(*job.older.generate(pos), job.older),
                                                    std::make_shared<const QcChunkShape>(*job.newer.generate(pos), job.newer)};
    };
    if (!runAll(ring.size(), work, {control, job.threads, 0, 2 * ring.size()})) {
        return std::nullopt;
    }
    QcBlendSources byChunk;
    byChunk.reserve(ring.size());
    for (qsizetype i = 0; i < ring.size(); ++i) {
        byChunk.insert(ring.at(i), std::move(sources[static_cast<std::size_t>(i)]));
    }
    return byChunk;
}

// The structures `older` builds into the chunks of `ring` that lie wholly in `old` and `ring`.
QList<QcXzRect> structuresWithin(const QcWorldGenerator &older, const QcChunkSet &old, const QcChunkSet &ring)
{
    const auto isInside = [&](QcChunkPos chunk) { return old.contains(chunk) || ring.contains(chunk); };
    QList<QcXzRect> held;
    const QSet<QcChunkPos> chunks = ring.chunks();
    for (const QcChunkPos chunk : chunks) {
        const QList<QcXzRect> reaches = older.structuresTouching(chunk);
        for (const QcXzRect reach : reaches) {
            if (!held.contains(reach) && std::ranges::all_of(reach.chunks(), isInside)) {
                held.append(reach);
            }
        }
    }
    return held;
}

} // namespace

namespace QcRingBlender {

std::optional<QcBlendField> plan(const QcWorldGenerator &older, const QcChunkSet &old, int widthChunks, const QcConversionControl &control)
{
    QcBlendField field(old, widthChunks, {});
    for (int round = 0; round < planRounds; ++round) {
        control.report({QcConversionControl::Stage::Blend, 0, 0});
        if (control.stopped()) {
            return std::nullopt;
        }
        QList<QcXzRect> held = structuresWithin(older, old, field.ring());
        if (held.size() == field.held().size()) {
            return field;
        }
        field = QcBlendField(old, widthChunks, std::move(held));
    }
    QList<QcXzRect> last = structuresWithin(older, old, field.ring());
    return last.size() == field.held().size() ? field : field.heldWithinRing(std::move(last));
}

std::optional<QList<Blended>> blend(const Job &job, const QcBlendField &field, quint64 seed,
                                    const std::function<QByteArray(const QcChunk &)> &encode, const QcConversionControl &control)
{
    const QSet<QcChunkPos> chunks = field.ring().chunks();
    QList<QcChunkPos> ring(chunks.cbegin(), chunks.cend());
    std::ranges::sort(ring);
    const std::optional<QcBlendSources> sources = generateShapes(job, ring, control);
    if (!sources) {
        return std::nullopt;
    }

    const QcBlendGrid grid(field, *sources, job.older.seaTop(), seed);
    QList<Blended> blended(ring.size());
    const auto work = [&](qsizetype index) {
        const QcChunkPos pos = ring.at(index);
        const std::shared_ptr<const QcChunk> older = job.older.generate(pos);
        const std::shared_ptr<const QcChunk> newer = job.newer.generate(pos);
        blended[index] = {pos, encode(*QcBlendedChunk({*older, *newer}, *sources, grid, job.older).build())};
    };
    if (!runAll(ring.size(), work, {control, job.threads, ring.size(), 2 * ring.size()})) {
        return std::nullopt;
    }
    return blended;
}

} // namespace QcRingBlender

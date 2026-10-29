// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Chunk snapshots read on worker threads while the owner keeps writing (decision E-844): the
// owner copies the chunk before each write (as QcClientWorld and QcServerWorld do), so the packed
// indices a reader holds never change, also across a repack to a wider index width.

#include "common/world/QcChunk.h"

#include <QTest>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

// The block the writer puts at `cell` in round `round`: many distinct ids, so the palette grows
// from 4 over 8 to 16 bits per index while the readers run.
QcBlockId blockFor(int round, int cell)
{
    return static_cast<QcBlockId>((round * 31 + cell) % 400 + 1);
}

// A snapshot with the round it was taken in; the cells written up to that round are known.
struct Snapshot
{
    std::shared_ptr<const QcChunk> chunk;
    int rounds = 0;
};

// Rounds of the writer: each writes `cellsPerRound` cells of section 0.
constexpr int writerRounds = 400;
constexpr int cellsPerRound = 64;

int cellOf(int round, int i)
{
    return (round * cellsPerRound + i) % QcWorld::blocksPerSection;
}

// The block of `cell` after `rounds` rounds of the writer (air if never written).
QcBlockId expectedBlock(int rounds, int cell)
{
    for (int round = rounds - 1; round >= 0; --round) {
        for (int i = 0; i < cellsPerRound; ++i) {
            if (cellOf(round, i) == cell) {
                return blockFor(round, i);
            }
        }
    }
    return qcAirBlockId;
}

} // namespace

class TestSectionSnapshots : public QObject
{
    Q_OBJECT

private slots:
    void readersSeeTheirSnapshotWhileTheOwnerWrites()
    {
        std::mutex mutex;
        Snapshot latest{std::make_shared<const QcChunk>(QcChunkPos{0, 0}), 0};
        std::atomic<bool> done = false;
        std::atomic<int> checked = 0;
        std::atomic<int> mismatches = 0;

        const auto reader = [&] {
            while (!done.load()) {
                Snapshot snapshot;
                {
                    const std::scoped_lock lock(mutex);
                    snapshot = latest;
                }
                // Reads every cell twice, with the writer running: both passes must agree.
                const QcChunkSection &section = snapshot.chunk->section(0);
                for (int cell = 0; cell < QcWorld::blocksPerSection; cell += 7) {
                    const QcBlockId first = section.block(cell % 16, cell / 256, cell / 16 % 16);
                    if (first != section.block(cell % 16, cell / 256, cell / 16 % 16)) {
                        ++mismatches;
                    }
                }
                // Spot check against the writer's known history.
                const int cell = (snapshot.rounds * 13) % QcWorld::blocksPerSection;
                if (section.block(cell % 16, cell / 256, cell / 16 % 16) != expectedBlock(snapshot.rounds, cell)) {
                    ++mismatches;
                }
                ++checked;
            }
        };

        std::vector<std::thread> readers;
        readers.reserve(3);
        for (int i = 0; i < 3; ++i) {
            readers.emplace_back(reader);
        }

        std::shared_ptr<QcChunk> owned = std::make_shared<QcChunk>(QcChunkPos{0, 0});
        std::vector<int> widths;
        for (int round = 0; round < writerRounds; ++round) {
            // Copy on write, as the client world does for every block update.
            owned = std::make_shared<QcChunk>(*owned);
            for (int i = 0; i < cellsPerRound; ++i) {
                const int cell = cellOf(round, i);
                owned->setBlock(cell % 16, cell / 256, cell / 16 % 16, blockFor(round, i));
                if (widths.empty() || widths.back() != owned->section(0).bitsPerIndex()) {
                    widths.push_back(owned->section(0).bitsPerIndex());
                }
            }
            const std::scoped_lock lock(mutex);
            latest = {owned, round + 1};
        }

        // Let the readers see the last snapshot at least once.
        const int seen = checked.load();
        QTRY_VERIFY_WITH_TIMEOUT(checked.load() > seen + 3, 10000);
        done = true;
        for (std::thread &thread : readers) {
            thread.join();
        }

        QCOMPARE(mismatches.load(), 0);
        QCOMPARE(widths, (std::vector<int>{4, 8, 16}));
        for (int cell = 0; cell < QcWorld::blocksPerSection; cell += 101) {
            QCOMPARE(owned->section(0).block(cell % 16, cell / 256, cell / 16 % 16), expectedBlock(writerRounds, cell));
        }
    }
};

QTEST_GUILESS_MAIN(TestSectionSnapshots)
#include "test_section_snapshots.moc"

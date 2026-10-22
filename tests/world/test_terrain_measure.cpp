#include "common/world/QcChunk.h"
#include "QcTestUtil.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QElapsedTimer>
#include <QTest>

#include <algorithm>
#include <limits>

// A measurement, not a check: set QTCRAFT_TERRAIN_MEASURE=1 and use a Release build. It times the
// terrain generator (stage 69: rivers) over fixed fields of chunks and columns of a few seeds, the
// fastest of a few runs; without the variable it only runs a small field once, as a smoke test.
class TestTerrainMeasure : public QObject
{
    Q_OBJECT

private:
    static bool full() { return qEnvironmentVariableIsSet("QTCRAFT_TERRAIN_MEASURE"); }

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    static quint64 chunkSum(const QcChunk &chunk)
    {
        quint64 sum = 14695981039346656037ULL;
        for (int y = 0; y < QcWorld::worldHeight; ++y) {
            for (int z = 0; z < QcWorld::chunkSize; ++z) {
                for (int x = 0; x < QcWorld::chunkSize; ++x) {
                    sum = (sum ^ chunk.block(x, y, z)) * 1099511628211ULL;
                }
            }
        }
        return sum;
    }

    // Milliseconds per chunk over a square of `edge` chunks from `corner`, the fastest of `runs`,
    // each with a new generator (nothing remembered from the run before).
    double perChunk(qint64 seed, QcChunkPos corner, int edge, int runs) const
    {
        double best = std::numeric_limits<double>::max();
        for (int run = 0; run < runs; ++run) {
            const QcTerrainGenerator generator(m_content, seed);
            QElapsedTimer timer;
            timer.start();
            for (int dz = 0; dz < edge; ++dz) {
                for (int dx = 0; dx < edge; ++dx) {
                    const std::shared_ptr<QcChunk> chunk = generator.generate({corner.x + dx, corner.z + dz});
                    if (!chunk) {
                        QTest::qFail("no chunk", __FILE__, __LINE__);
                    }
                }
            }
            best = std::min(best, double(timer.nsecsElapsed()) / 1.0e6 / double(edge * edge));
        }
        return best;
    }

    // Microseconds per column over a square of `edge` columns from `corner`, the fastest of `runs`,
    // each with a new generator.
    double perColumn(qint64 seed, std::pair<int, int> corner, int edge, int runs) const
    {
        double best = std::numeric_limits<double>::max();
        qint64 sum = 0;
        for (int run = 0; run < runs; ++run) {
            const QcTerrainGenerator generator(m_content, seed);
            QElapsedTimer timer;
            timer.start();
            for (int z = 0; z < edge; ++z) {
                for (int x = 0; x < edge; ++x) {
                    sum += generator.column(corner.first + x, corner.second + z).height;
                }
            }
            best = std::min(best, double(timer.nsecsElapsed()) / 1.0e3 / double(edge * edge));
        }
        // Uses the heights, so the loop is not optimised away.
        if (sum <= 0) {
            QTest::qFail("no ground", __FILE__, __LINE__);
        }
        return best;
    }

private slots:
    // A checksum of the blocks of a few fields of chunks: variants of the generator that should make
    // the same world (an optimisation) print the same value.
    void checksum()
    {
        const int edge = full() ? 12 : 2;
        quint64 sum = 0;
        for (const qint64 seed : {qint64(1), qint64(7), qint64(42), qint64(1234)}) {
            const QcTerrainGenerator generator(m_content, seed);
            for (int dz = 0; dz < edge; ++dz) {
                for (int dx = 0; dx < edge; ++dx) {
                    const std::shared_ptr<QcChunk> chunk = generator.generate({dx - edge / 2, dz - edge / 2});
                    sum = sum * 1099511628211ULL + chunkSum(*chunk);
                }
            }
        }
        qInfo("TERRAIN checksum %016llx", sum);
        QVERIFY(sum != 0);
    }

    void chunksAndColumns()
    {
        const int edge = full() ? 24 : 2;
        const int runs = full() ? 3 : 1;
        const QList<QcChunkPos> fields = full() ? QList<QcChunkPos>{{-40, -40}, {10, -12}, {30, 30}} : QList<QcChunkPos>{{0, 0}};
        double chunkSum = 0.0;
        double columnSum = 0.0;
        int samples = 0;
        for (const qint64 seed : {qint64(1), qint64(7), qint64(42), qint64(1234)}) {
            for (const QcChunkPos corner : fields) {
                const double chunk = perChunk(seed, corner, edge, runs);
                const double column = perColumn(seed, {corner.x * QcWorld::chunkSize, corner.z * QcWorld::chunkSize}, edge * 8, runs);
                qInfo("seed %lld, chunks from %d %d: %.3f ms per chunk, %.3f us per column", seed, corner.x, corner.z, chunk, column);
                chunkSum += chunk;
                columnSum += column;
                ++samples;
            }
            if (!full()) {
                break;
            }
        }
        qInfo("TERRAIN %.3f ms per chunk, %.3f us per column (mean of %d fields)", chunkSum / samples, columnSum / samples, samples);
    }
};

QTEST_GUILESS_MAIN(TestTerrainMeasure)
#include "test_terrain_measure.moc"

// The pages of the section arena (decision E-820) on QRhi's Null backend, so the page logic runs on
// every platform: slots share a page, a released range is reused exactly after the frames in flight
// and one more, a section larger than a page gets its own, a full index part rolls the vertex range
// back, a release after clear() is harmless, one empty shared page stays in reserve, and the
// per-section layout gives every section a page of its size.

#include "app/rendering/QcSectionArena.h"

#include <QTest>
#include <rhi/qrhi.h>

#include <memory>

namespace {

constexpr quint32 stride = QcSectionArena::vertexStride;
constexpr quint32 word = QcSectionArena::indexWord;

void advance(QcSectionArena &arena, quint64 frames)
{
    for (quint64 frame = 0; frame < frames; ++frame) {
        arena.nextFrame();
    }
}

// The vertices (and index words, the same number) of the first shared page; the page stays, empty,
// in reserve, so the following allocations start at its offset 0.
quint32 firstPageUnits(QcSectionArena &arena)
{
    const QcArenaSlot slot = arena.allocate(stride, word).value();
    const auto units = quint32(arena.bytes() / (stride + word));
    arena.release(slot);
    advance(arena, arena.reuseDelay());
    return units;
}

} // namespace

class TestSectionArena : public QObject
{
    Q_OBJECT

    std::unique_ptr<QRhi> m_rhi;

    QcSectionArena shared()
    {
        QcSectionArena arena;
        arena.reset(m_rhi.get(), QRhiBuffer::UsageFlags(), QcSectionArena::Layout::Shared);
        return arena;
    }

private slots:
    void initTestCase()
    {
        QRhiNullInitParams params;
        m_rhi.reset(QRhi::create(QRhi::Null, &params));
        QVERIFY(m_rhi);
    }

    void slotsSharePageInOrder()
    {
        QcSectionArena arena = shared();
        const QcArenaSlot a = arena.allocate(2 * stride, 3 * word).value();
        const QcArenaSlot b = arena.allocate(stride, word).value();
        QCOMPARE(arena.pageCount(), 1);
        QCOMPARE(a.page, b.page);
        QCOMPARE(a.vertices, b.vertices);
        QCOMPARE(a.vertexOffset, 0u);
        QCOMPARE(a.indexOffset, 0u);
        QCOMPARE(b.vertexOffset, 2 * stride);
        QCOMPARE(b.indexOffset, 3 * word);
        QCOMPARE(a.bytes(), quint64(2 * stride + 3 * word));
    }

    void releasedRangeIsReusedExactlyAfterTheDelay()
    {
        QcSectionArena arena = shared();
        QCOMPARE(arena.reuseDelay(), quint64(m_rhi->resourceLimit(QRhi::FramesInFlight)) + 1);
        const QcArenaSlot first = arena.allocate(stride, word).value();
        QVERIFY(arena.allocate(stride, word).has_value()); // keeps the page occupied
        arena.release(first);

        // One frame short of the delay the range is still held back...
        advance(arena, arena.reuseDelay() - 1);
        const QcArenaSlot early = arena.allocate(stride, word).value();
        QVERIFY(early.vertexOffset != first.vertexOffset);
        arena.release(early);

        // ...one frame later it is the first free range again.
        arena.nextFrame();
        const QcArenaSlot reused = arena.allocate(stride, word).value();
        QCOMPARE(reused.vertexOffset, first.vertexOffset);
        QCOMPARE(reused.indexOffset, first.indexOffset);
    }

    void sectionLargerThanAPageGetsItsOwn()
    {
        QcSectionArena arena = shared();
        const quint32 units = firstPageUnits(arena);
        const QcArenaSlot small = arena.allocate(stride, word).value();
        // Larger than even the largest page (eight times the first): a page of exactly its size.
        const quint32 huge = 8 * units + 8;
        const QcArenaSlot large = arena.allocate(huge * stride, word).value();
        QCOMPARE(arena.pageCount(), 2);
        QVERIFY(large.page != small.page);
        QCOMPARE(large.vertexOffset, 0u);
        QCOMPARE(large.vertices->size(), huge * stride);
    }

    void fullIndexPartRollsTheVertexRangeBack()
    {
        QcSectionArena arena = shared();
        const quint32 units = firstPageUnits(arena);
        // All index words but one vertex short of the page.
        const QcArenaSlot wide = arena.allocate((units - 1) * stride, units * word).value();
        // One vertex fits, its index word does not: a new page, and the vertex is given back.
        const QcArenaSlot next = arena.allocate(stride, word).value();
        QCOMPARE(arena.pageCount(), 2);
        QVERIFY(next.page != wide.page);

        arena.release(wide);
        advance(arena, arena.reuseDelay());
        // The whole first page is free again only if the failed try left no vertex behind.
        const QcArenaSlot whole = arena.allocate(units * stride, word).value();
        QCOMPARE(whole.page, wide.page);
    }

    void releaseAfterClearIsIgnored()
    {
        QcSectionArena arena = shared();
        const QcArenaSlot slot = arena.allocate(stride, word).value();
        arena.clear();
        QCOMPARE(arena.pageCount(), 0);
        arena.release(slot);
        advance(arena, arena.reuseDelay());
        QCOMPARE(arena.pageCount(), 0);
        // A page created after the clear has a new serial, so the stale slot cannot free its ranges.
        const QcArenaSlot fresh = arena.allocate(stride, word).value();
        QVERIFY(fresh.page != slot.page);
    }

    void oneEmptySharedPageStaysInReserve()
    {
        QcSectionArena arena = shared();
        const quint32 units = firstPageUnits(arena);
        const QcArenaSlot a = arena.allocate(stride, word).value();
        const QcArenaSlot b = arena.allocate(units * stride, word).value();
        QCOMPARE(arena.pageCount(), 2);
        arena.release(a);
        arena.release(b);
        advance(arena, arena.reuseDelay());
        QCOMPARE(arena.pageCount(), 1);
        QVERIFY(arena.bytes() > 0);
        // The reserve serves the next section without a new page.
        const QcArenaSlot again = arena.allocate(stride, word).value();
        QCOMPARE(arena.pageCount(), 1);
        QVERIFY(again.page == a.page || again.page == b.page);
    }

    void perSectionLayoutSizesEachPageToItsSection()
    {
        QcSectionArena arena;
        arena.reset(m_rhi.get(), QRhiBuffer::UsageFlags(), QcSectionArena::Layout::PerSection);
        const QcArenaSlot a = arena.allocate(5 * stride, 7 * word).value();
        const QcArenaSlot b = arena.allocate(stride, word).value();
        QCOMPARE(arena.pageCount(), 2);
        QVERIFY(a.page != b.page);
        QCOMPARE(a.vertices->size(), 5 * stride);
        QCOMPARE(a.indices->size(), 7 * word);
        QCOMPARE(arena.bytes(), quint64(6 * stride + 8 * word));

        // No reserve: a removed section's page goes away.
        arena.release(a);
        advance(arena, arena.reuseDelay());
        QCOMPARE(arena.pageCount(), 1);
        QCOMPARE(arena.bytes(), quint64(stride + word));
    }
};

QTEST_MAIN(TestSectionArena)
#include "test_section_arena.moc"

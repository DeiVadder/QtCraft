#pragma once

#include "app/rendering/QcRangeAllocator.h"

#include <rhi/qrhi.h>

#include <memory>
#include <optional>
#include <vector>

// Where the data of one section lies in the arena: the buffers of its page and the byte offsets of
// its vertices and indices there. Invalid (no buffers) for a section without faces.
struct QcArenaSlot
{
    QRhiBuffer *vertices = nullptr;
    QRhiBuffer *indices = nullptr;
    quint32 vertexOffset = 0;
    quint32 indexOffset = 0;
    quint32 vertexBytes = 0;
    quint32 indexBytes = 0;
    // The page's serial number: draws are grouped by it, so one binding serves many sections.
    quint32 page = 0;

    bool isValid() const { return vertices != nullptr; }
    quint64 bytes() const { return static_cast<quint64>(vertexBytes) + indexBytes; }
};

// The section buffers of the world as pages, each a vertex and an index buffer (decision E-820).
// With the shared layout a page holds many sections: QRhi's Metal backend spends about a microsecond
// of allocations in every setVertexInput, so drawing a page's sections with one binding and base
// vertex/first index per draw saves most of the render thread's time in the opaque pass. A section's
// vertices and indices always lie in the same page. Released ranges are reused only after the frames
// that may still read them on the GPU have finished. With the per-section layout every section is a
// page of its own size: for backends that fill buffers through resource update batches, where a
// partial upload into a large page would stage or copy the whole page. Render thread only.
class QcSectionArena
{
public:
    // Bytes per vertex of a section mesh (QcBlockVertex); vertex ranges are handed out in vertices.
    static constexpr quint32 vertexStride = 24;
    // Index data is handed out in 4-byte words, so every list keeps its 4-byte alignment.
    static constexpr quint32 indexWord = 4;

    enum class Layout : quint8 {
        // Pages of 2 to 16 MiB of vertices shared by many sections; one empty page stays in reserve.
        Shared,
        // One page per section, sized to it; an empty page goes away.
        PerSection
    };

    // Forgets every page, for a new QRhi; `usage` is added to the vertex and index usage of the
    // pages' buffers (the storage usage for direct writes, decision E-813).
    void reset(QRhi *rhi, QRhiBuffer::UsageFlags usage, Layout layout);
    // Drops every page at once (all sections removed); QRhi releases the buffers once the GPU is
    // done with them.
    void clear();
    // Room for `vertexBytes` (a multiple of vertexStride) and `indexBytes` (a multiple of indexWord)
    // in one page, in a new page when none has it; nothing when the QRhi cannot create the page.
    [[nodiscard]] std::optional<QcArenaSlot> allocate(quint32 vertexBytes, quint32 indexBytes);
    // Gives the slot's ranges back once the frames in flight that may draw from them are done.
    void release(const QcArenaSlot &slot);
    // Called once per frame before the frame's allocations: ranges released long enough ago become
    // free again, and pages without sections go away (but for the reserve of the shared layout).
    void nextFrame();

    // GPU bytes of all pages (their capacity, used or not).
    quint64 bytes() const;
    int pageCount() const { return static_cast<int>(m_pages.size()); }
    Layout layout() const { return m_layout; }
    // Frames a released range waits before it is reused: the frames in flight and one more.
    quint64 reuseDelay() const { return m_delay; }

private:
    struct Page
    {
        quint32 serial = 0;
        std::unique_ptr<QRhiBuffer> vertices;
        std::unique_ptr<QRhiBuffer> indices;
        QcRangeAllocator vertexUnits;
        QcRangeAllocator indexWords;
    };
    struct Pending
    {
        QcArenaSlot slot;
        quint64 frame = 0;
    };

    // A new page with at least this many vertices and index words; null when the QRhi refuses it.
    Page *createPage(quint32 vertexCount, quint32 wordCount);
    // The slot in `page`, when both ranges fit.
    static std::optional<QcArenaSlot> place(Page &page, quint32 vertexCount, quint32 wordCount);
    // Returns the slot's ranges to its page right away.
    void free(const QcArenaSlot &slot);
    // Drops the empty pages, keeping one in reserve with the shared layout.
    void dropEmptyPages();

    QRhi *m_rhi = nullptr;
    QRhiBuffer::UsageFlags m_usage;
    Layout m_layout = Layout::Shared;
    std::vector<std::unique_ptr<Page>> m_pages;
    std::vector<Pending> m_pending;
    quint64 m_frame = 0;
    quint64 m_delay = 3;
    quint32 m_nextSerial = 0;
};

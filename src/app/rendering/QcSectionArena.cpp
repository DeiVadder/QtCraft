#include "app/rendering/QcSectionArena.h"

#include "app/debug/QcAppLogging.h"
#include "clientcore/meshing/QcChunkMesh.h"

#include <algorithm>

static_assert(QcSectionArena::vertexStride == sizeof(QcBlockVertex));

namespace {

// Shared pages grow from 2 MiB to 16 MiB of vertices, so a small view distance does not hold 16 MiB
// per page for a few sections and a large one needs few pages (and few bindings per pass).
constexpr quint32 firstPageVertices = (2u << 20) / QcSectionArena::vertexStride;
constexpr quint32 largestPageVertices = (16u << 20) / QcSectionArena::vertexStride;
// One index word per vertex: a quad has four vertices and six two-byte indices (three bytes per
// vertex), so the index part has a third to spare for padding and four-byte indices.
constexpr quint32 wordsPerVertex = 1;

} // namespace

void QcSectionArena::reset(QRhi *rhi, QRhiBuffer::UsageFlags usage, Layout layout)
{
    clear();
    m_rhi = rhi;
    m_usage = usage;
    m_layout = layout;
    m_frame = 0;
    m_delay = rhi ? static_cast<quint64>(rhi->resourceLimit(QRhi::FramesInFlight)) + 1 : 3;
}

void QcSectionArena::clear()
{
    m_pending.clear();
    m_pages.clear();
}

QcSectionArena::Page *QcSectionArena::createPage(quint32 vertexCount, quint32 wordCount)
{
    quint32 vertices = vertexCount;
    quint32 words = wordCount;
    if (m_layout == Layout::Shared) {
        const int grown = std::min(static_cast<int>(m_pages.size()), 3);
        const quint32 standard = std::min(largestPageVertices, firstPageVertices << grown);
        vertices = std::max(standard, vertexCount);
        words = std::max(standard * wordsPerVertex, wordCount);
    }

    std::unique_ptr<QRhiBuffer> vertexBuffer(
        m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer | m_usage, vertices * vertexStride));
    std::unique_ptr<QRhiBuffer> indexBuffer(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer | m_usage, words * indexWord));
    if (!vertexBuffer->create() || !indexBuffer->create()) {
        qCWarning(lcRenderer).nospace() << "Section page of " << vertices * vertexStride << " + " << words * indexWord
                                        << " bytes could not be created";
        return nullptr;
    }

    auto page = std::make_unique<Page>(Page{m_nextSerial++, std::move(vertexBuffer), std::move(indexBuffer),
                                            QcRangeAllocator(vertices), QcRangeAllocator(words)});
    m_pages.push_back(std::move(page));
    return m_pages.back().get();
}

std::optional<QcArenaSlot> QcSectionArena::place(Page &page, quint32 vertexCount, quint32 wordCount)
{
    const std::optional<quint32> firstVertex = page.vertexUnits.allocate(vertexCount);
    if (!firstVertex) {
        return std::nullopt;
    }
    const std::optional<quint32> firstWord = page.indexWords.allocate(wordCount);
    if (!firstWord) {
        page.vertexUnits.release(*firstVertex, vertexCount);
        return std::nullopt;
    }
    return QcArenaSlot{page.vertices.get(), page.indices.get(), *firstVertex * vertexStride, *firstWord * indexWord,
                       vertexCount * vertexStride, wordCount * indexWord, page.serial};
}

std::optional<QcArenaSlot> QcSectionArena::allocate(quint32 vertexBytes, quint32 indexBytes)
{
    const quint32 vertexCount = vertexBytes / vertexStride;
    const quint32 wordCount = indexBytes / indexWord;
    if (vertexCount == 0 || wordCount == 0 || vertexCount * vertexStride != vertexBytes || wordCount * indexWord != indexBytes) {
        qCWarning(lcRenderer).nospace() << "Section data of " << vertexBytes << " + " << indexBytes << " bytes does not fit the page units";
        return std::nullopt;
    }

    // A per-section page is full from the start, so only shared pages are worth a look.
    if (m_layout == Layout::Shared) {
        for (const std::unique_ptr<Page> &page : m_pages) {
            if (std::optional<QcArenaSlot> slot = place(*page, vertexCount, wordCount)) {
                return slot;
            }
        }
    }
    Page *page = createPage(vertexCount, wordCount);
    return page ? place(*page, vertexCount, wordCount) : std::nullopt;
}

void QcSectionArena::release(const QcArenaSlot &slot)
{
    if (slot.isValid()) {
        m_pending.push_back({slot, m_frame});
    }
}

void QcSectionArena::free(const QcArenaSlot &slot)
{
    const auto page = std::ranges::find(m_pages, slot.page, [](const std::unique_ptr<Page> &candidate) { return candidate->serial; });
    if (page == m_pages.end()) {
        return; // the page went with clear()
    }
    (*page)->vertexUnits.release(slot.vertexOffset / vertexStride, slot.vertexBytes / vertexStride);
    (*page)->indexWords.release(slot.indexOffset / indexWord, slot.indexBytes / indexWord);
}

void QcSectionArena::nextFrame()
{
    ++m_frame;
    // A range last drawn before its release in frame f is no longer read once the frames in flight
    // after f have finished; QRhi waits for them before it starts frame f + m_delay.
    const auto matured = std::ranges::partition(m_pending, [this](const Pending &pending) { return pending.frame + m_delay > m_frame; });
    if (matured.empty()) {
        return;
    }
    for (const Pending &pending : matured) {
        free(pending.slot);
    }
    m_pending.erase(matured.begin(), matured.end());
    dropEmptyPages();
}

void QcSectionArena::dropEmptyPages()
{
    // Only pages emptied just now can be empty; QRhi releases their buffers after the GPU is done.
    // One empty shared page stays, so sections coming and going at the edge of the view do not
    // destroy and recreate a page every few frames.
    bool reserved = m_layout != Layout::Shared;
    std::erase_if(m_pages, [&reserved](const std::unique_ptr<Page> &page) {
        if (!page->vertexUnits.isEmpty()) {
            return false;
        }
        return std::exchange(reserved, true);
    });
}

quint64 QcSectionArena::bytes() const
{
    quint64 total = 0;
    for (const std::unique_ptr<Page> &page : m_pages) {
        total += page->vertices->size() + page->indices->size();
    }
    return total;
}

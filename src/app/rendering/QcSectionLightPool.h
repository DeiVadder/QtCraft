#pragma once

#include "clientcore/light/QcSectionLight.h"

#include <QtGlobal>
#include <rhi/qrhi.h>

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

// The per-cell light of the sections on the GPU (decision E-390), for shaders that read light per
// cell instead of from the vertices (soft light, moving light, fog in the realistic mode of stage
// 45; today's shaders do not read it). Pages of 3D textures (RG8: block light, sky light, level × 17)
// hold the sections in slots of 16 x 16 x 16 texels stacked along the depth: slot s of a page covers
// the slices 16 s to 16 s + 15, texel (x, z, slice) is the cell (x, y, z). A section whose channels
// are uniform takes no slot, only its two levels (most sections: dark or fully lit by the sky); a
// draw passes the slot or the levels to its shaders. Render thread only.
class QcSectionLightPool
{
public:
    // Where a section's light lies: a slot of a page, or (page < 0) the uniform levels.
    struct Slot
    {
        int page = -1;
        int index = 0;
        quint8 uniformBlock = 0;
        quint8 uniformSky = 0;

        bool isUniform() const { return page < 0; }
        // First slice of the slot in its page.
        int firstSlice() const { return index * sectionSize; }
    };

    static constexpr int sectionSize = 16;
    // 64 slots: 16 x 16 x 1024 texels, 512 KiB per page.
    static constexpr int defaultSlotsPerPage = 64;

    explicit QcSectionLightPool(int slotsPerPage = defaultSlotsPerPage);

    // Whether the backend can hold the pages (3D textures in RG8).
    static bool isSupported(QRhi *rhi);

    // Stores the light of section `key` in place of its earlier light; false when the GPU refused a
    // new page (the section then has no light and slot() is empty).
    bool update(QRhi *rhi, QRhiResourceUpdateBatch *updates, quint64 key, const QcSectionLight &light);
    void release(quint64 key);
    // Forgets every section and drops the pages (a new QRhi, or the light is no longer read).
    // Released slots go back to the free list, but their pages stay until then: the sections with
    // light are bounded by the view distance, so the pages of the busiest moment are what the next
    // area needs again, and freeing them would create and destroy 512 KiB textures whenever sections
    // come and go at the edge of a page.
    void reset();

    std::optional<Slot> slot(quint64 key) const;
    QRhiTexture *page(int index) const { return m_pages[static_cast<std::size_t>(index)].get(); }
    int pageCount() const { return static_cast<int>(m_pages.size()); }
    int slotsPerPage() const { return m_slotsPerPage; }
    int usedSlots() const { return m_usedSlots; }
    qsizetype sections() const { return static_cast<qsizetype>(m_slots.size()); }
    // GPU bytes of all pages.
    quint64 bytes() const;

private:
    // A free slot, on a new page when every page is full; nullopt when the GPU refused the page.
    std::optional<Slot> takeSlot(QRhi *rhi);
    void upload(QRhiResourceUpdateBatch *updates, Slot slot, const QcSectionLight &light) const;

    int m_slotsPerPage = defaultSlotsPerPage;
    std::vector<std::unique_ptr<QRhiTexture>> m_pages;
    // Free slots as page * slotsPerPage + index; taken from the back.
    std::vector<int> m_free;
    std::unordered_map<quint64, Slot> m_slots;
    int m_usedSlots = 0;
};

#include "app/rendering/QcSectionLightPool.h"

#include "app/debug/QcAppLogging.h"


namespace {

constexpr int texelsPerSlice = QcSectionLightPool::sectionSize * QcSectionLightPool::sectionSize;
constexpr int sliceBytes = texelsPerSlice * QcSectionLight::texelBytes;

} // namespace

QcSectionLightPool::QcSectionLightPool(int slotsPerPage)
    : m_slotsPerPage(slotsPerPage)
{
}

bool QcSectionLightPool::isSupported(QRhi *rhi)
{
    return rhi && rhi->isFeatureSupported(QRhi::ThreeDimensionalTextures)
           && rhi->isTextureFormatSupported(QRhiTexture::RG8, QRhiTexture::ThreeDimensional);
}

bool QcSectionLightPool::update(QRhi *rhi, QRhiResourceUpdateBatch *updates, quint64 key, const QcSectionLight &light)
{
    if (light.isUniform()) {
        release(key);
        m_slots.insert_or_assign(key, Slot{-1, 0, light.uniformBlock, light.uniformSky});
        return true;
    }

    // A section that already has a slot keeps it.
    std::optional<Slot> slot;
    if (const auto found = m_slots.find(key); found != m_slots.end() && !found->second.isUniform()) {
        slot = found->second;
    } else {
        slot = takeSlot(rhi);
    }
    if (!slot) {
        m_slots.erase(key);
        return false;
    }

    upload(updates, *slot, light);
    m_slots.insert_or_assign(key, *slot);
    return true;
}

void QcSectionLightPool::release(quint64 key)
{
    const auto found = m_slots.find(key);
    if (found == m_slots.end()) {
        return;
    }

    if (!found->second.isUniform()) {
        m_free.push_back(found->second.page * m_slotsPerPage + found->second.index);
        --m_usedSlots;
    }
    m_slots.erase(found);
}

void QcSectionLightPool::reset()
{
    m_pages.clear();
    m_free.clear();
    m_slots.clear();
    m_usedSlots = 0;
}

std::optional<QcSectionLightPool::Slot> QcSectionLightPool::slot(quint64 key) const
{
    const auto found = m_slots.find(key);
    return found == m_slots.end() ? std::nullopt : std::optional<Slot>(found->second);
}

quint64 QcSectionLightPool::bytes() const
{
    return quint64(m_pages.size()) * quint64(m_slotsPerPage) * sectionSize * sliceBytes;
}

std::optional<QcSectionLightPool::Slot> QcSectionLightPool::takeSlot(QRhi *rhi)
{
    if (m_free.empty()) {
        std::unique_ptr<QRhiTexture> page(rhi->newTexture(QRhiTexture::RG8, sectionSize, sectionSize, sectionSize * m_slotsPerPage, 1,
                                                          QRhiTexture::ThreeDimensional));
        if (!page->create()) {
            qCWarning(lcRendererLight) << "Cannot create a page for the light of" << m_slotsPerPage << "sections";
            return std::nullopt;
        }

        const int pageIndex = int(m_pages.size());
        m_pages.push_back(std::move(page));
        // Taken from the back: the first slot of the page first.
        for (int index = m_slotsPerPage - 1; index >= 0; --index) {
            m_free.push_back(pageIndex * m_slotsPerPage + index);
        }
    }

    const int id = m_free.back();
    m_free.pop_back();
    ++m_usedSlots;
    return Slot{id / m_slotsPerPage, id % m_slotsPerPage, 0, 0};
}

void QcSectionLightPool::upload(QRhiResourceUpdateBatch *updates, Slot slot, const QcSectionLight &light) const
{
    // One upload entry per slice (a layer of a 3D texture is a slice); the texels are already in
    // slice order (y slowest).
    const QByteArray texels = light.texels();
    QList<QRhiTextureUploadEntry> entries;
    entries.reserve(sectionSize);
    for (int y = 0; y < sectionSize; ++y) {
        const QRhiTextureSubresourceUploadDescription slice(texels.constData() + qsizetype(y) * sliceBytes, quint32(sliceBytes));
        entries.append(QRhiTextureUploadEntry(slot.firstSlice() + y, 0, slice));
    }

    QRhiTextureUploadDescription description;
    description.setEntries(entries.cbegin(), entries.cend());
    updates->uploadTexture(page(slot.page), description);
}

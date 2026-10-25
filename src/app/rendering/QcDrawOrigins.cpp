#include "app/rendering/QcDrawOrigins.h"

#include "clientcore/camera/QcRenderOrigin.h"

#include <algorithm>

namespace {

// Entries the buffer holds at least, so the first frames do not grow it section by section.
constexpr quint32 minimumEntries = 1024;
// Written entries this close together go up in one run: fewer, slightly larger copies.
constexpr quint32 runGap = 8;

} // namespace

QVector3D QcDrawOrigins::follow(const QcVec3 &camera)
{
    if (!m_anchored || QcRenderOrigin::outOfReach(m_anchor, camera)) {
        m_anchor = QcRenderOrigin::blockNear(camera);
        m_anchored = true;
        for (qsizetype entry = 0; entry < m_entries.size(); ++entry) {
            m_entries[entry] = QcRenderOrigin::entry(m_origins.at(entry).position, m_anchor, m_origins.at(entry).cell);
        }
        m_allWritten = true;
    }
    return QcRenderOrigin::entry(camera, m_anchor).toVector3D();
}

quint32 QcDrawOrigins::place(const Origin &origin)
{
    quint32 entry = 0;
    if (m_free.empty()) {
        entry = static_cast<quint32>(m_entries.size());
        m_origins.append(origin);
        m_entries.append(QVector4D());
    } else {
        entry = m_free.back();
        m_free.pop_back();
        m_origins[entry] = origin;
    }
    write(entry);
    return entry;
}

quint32 QcDrawOrigins::add(const QcVec3 &origin)
{
    return place({origin, 0.0f});
}

quint32 QcDrawOrigins::addSection(QcBlockPos corner)
{
    return place({QcRenderOrigin::toVec3(corner), QcRenderOrigin::sectionCell(corner)});
}

void QcDrawOrigins::move(quint32 entry, const QcVec3 &origin)
{
    // The same origin keeps its entry: nothing to upload.
    if (m_origins.at(entry).position == origin) {
        return;
    }
    m_origins[entry].position = origin;
    write(entry);
}

void QcDrawOrigins::release(quint32 entry)
{
    m_free.push_back(entry);
}

void QcDrawOrigins::write(quint32 entry)
{
    const Origin &origin = m_origins.at(entry);
    m_entries[entry] = QcRenderOrigin::entry(origin.position, m_anchor, origin.cell);
    if (!m_allWritten) {
        m_written.push_back(entry);
    }
}

quint32 QcDrawOrigins::upload(QRhi *rhi, QRhiResourceUpdateBatch *updates)
{
    const auto bytes = static_cast<quint32>(m_entries.size()) * stride;
    if (!m_buffer || m_buffer->size() < bytes) {
        m_buffer.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, std::max(bytes * 2, minimumEntries * stride)));
        m_buffer->create();
        m_allWritten = true;
    }
    if (!m_allWritten) {
        return uploadQueued(updates);
    }

    m_allWritten = false;
    m_written.clear();
    if (bytes > 0) {
        updates->updateDynamicBuffer(m_buffer.get(), 0, bytes, m_entries.constData());
    }
    return bytes;
}

quint32 QcDrawOrigins::uploadQueued(QRhiResourceUpdateBatch *updates)
{
    // The queue keeps its capacity: no allocation once it has seen a busy frame.
    std::ranges::sort(m_written);
    quint32 uploaded = 0;
    std::size_t start = 0;
    while (start < m_written.size()) {
        std::size_t end = start + 1;
        while (end < m_written.size() && m_written[end] - m_written[end - 1] <= runGap) {
            ++end;
        }
        const quint32 first = m_written[start];
        const quint32 bytes = (m_written[end - 1] - first + 1) * stride;
        updates->updateDynamicBuffer(m_buffer.get(), first * stride, bytes, &m_entries.at(first));
        uploaded += bytes;
        start = end;
    }
    m_written.clear();
    return uploaded;
}

void QcDrawOrigins::reset()
{
    m_buffer.reset();
    m_anchor = {};
    m_anchored = false;
    m_origins.clear();
    m_entries.clear();
    m_free.clear();
    m_written.clear();
    m_allWritten = false;
}

QRhiCommandBuffer::VertexInput QcDrawOrigins::inputAt(quint32 entry, bool baseInstance) const
{
    return {m_buffer.get(), baseInstance ? 0 : entry * stride};
}

QRhiVertexInputLayout QcDrawOrigins::withOrigins(const QRhiVertexInputLayout &layout)
{
    QRhiVertexInputLayout result = layout;
    QList<QRhiVertexInputBinding> bindings(layout.cbeginBindings(), layout.cendBindings());
    bindings.append(QRhiVertexInputBinding(stride, QRhiVertexInputBinding::PerInstance));
    result.setBindings(bindings.cbegin(), bindings.cend());
    QList<QRhiVertexInputAttribute> attributes(layout.cbeginAttributes(), layout.cendAttributes());
    attributes.append(QRhiVertexInputAttribute(binding, location, QRhiVertexInputAttribute::Float4, 0));
    result.setAttributes(attributes.cbegin(), attributes.cend());
    return result;
}

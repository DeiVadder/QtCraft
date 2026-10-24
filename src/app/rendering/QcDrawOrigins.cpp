#include "app/rendering/QcDrawOrigins.h"

#include "clientcore/camera/QcRenderOrigin.h"

#include <algorithm>

namespace {

// Entries the buffer holds at least, so the first frames do not grow it section by section.
constexpr quint32 minimumEntries = 1024;

} // namespace

void QcDrawOrigins::begin(const QcVec3 &camera)
{
    m_camera = camera;
    m_entries.clear();
}

quint32 QcDrawOrigins::add(const QcVec3 &origin)
{
    m_entries.append(QcRenderOrigin::entry(origin, m_camera));
    return static_cast<quint32>(m_entries.size() - 1);
}

quint32 QcDrawOrigins::addSection(QcBlockPos corner)
{
    m_entries.append(QcRenderOrigin::entry(QcRenderOrigin::toVec3(corner), m_camera, QcRenderOrigin::sectionCell(corner)));
    return static_cast<quint32>(m_entries.size() - 1);
}

quint32 QcDrawOrigins::upload(QRhi *rhi, QRhiResourceUpdateBatch *updates)
{
    const auto bytes = static_cast<quint32>(m_entries.size()) * stride;
    if (!m_buffer || m_buffer->size() < bytes) {
        m_buffer.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, std::max(bytes * 2, minimumEntries * stride)));
        m_buffer->create();
    }
    if (bytes > 0) {
        updates->updateDynamicBuffer(m_buffer.get(), 0, bytes, m_entries.constData());
    }
    return bytes;
}

void QcDrawOrigins::reset()
{
    m_buffer.reset();
    m_entries.clear();
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

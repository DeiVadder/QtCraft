#include "QcBlockRenderTable.h"

#include "QcContentRegistry.h"
#include "QcTextureAtlas.h"

QcBlockRenderTable::QcBlockRenderTable(const QcContentRegistry &content, const QcTextureAtlas &atlas)
{
    m_entries.reserve(content.blockCount());
    for (const QcBlockDefinition &block : content.blocks()) {
        Entry entry;
        entry.layer = block.renderLayer;
        entry.opaque = block.opaque;
        entry.liquid = block.liquid;
        for (const QcBlockFace face : qcAllBlockFaces) {
            entry.faceUv[std::size_t(face)] = atlas.uv(block.texture(face));
        }
        m_entries.append(entry);
    }
}

const QcBlockRenderTable::Entry &QcBlockRenderTable::entry(QcBlockId id) const
{
    return qsizetype(id) < m_entries.size() ? m_entries[qsizetype(id)] : m_invisible;
}

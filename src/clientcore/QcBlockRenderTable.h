#pragma once

#include "QcBlockDefinition.h"

#include <QList>
#include <QRectF>

#include <array>

class QcContentRegistry;
class QcTextureAtlas;

// Per-block data the mesher needs, indexed by runtime block id. Immutable after construction and
// shared with meshing threads.
class QcBlockRenderTable
{
public:
    struct Entry
    {
        QcRenderLayer layer = QcRenderLayer::Invisible;
        bool opaque = false;
        bool liquid = false;
        // Sky light lost when passing through this block, 0..15.
        int lightOpacity = 0;
        std::array<QRectF, 6> faceUv;
    };

    QcBlockRenderTable() = default;
    QcBlockRenderTable(const QcContentRegistry &content, const QcTextureAtlas &atlas);

    // Unknown ids render as invisible.
    const Entry &entry(QcBlockId id) const;
    qsizetype size() const { return m_entries.size(); }

private:
    QList<Entry> m_entries;
    Entry m_invisible;
};

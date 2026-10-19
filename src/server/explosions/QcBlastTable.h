#pragma once

#include "common/content/definitions/QcBlockDefinition.h"

#include <vector>

class QcContentRegistry;

// What explosions need to know about every block state of a content (decision E-600), looked up
// once: how much of a blast's force it soaks up, whether it stops the sight of a blast on bodies
// (solid blocks do) and whether it is an explosive that a blast lights instead of breaking.
class QcBlastTable
{
public:
    struct Entry
    {
        float resistance = 0.0f;
        bool solid = false;
        bool explosive = false;
    };

    explicit QcBlastTable(const QcContentRegistry &content);

    const Entry &of(QcBlockId block) const;
    // The explosive data of `block` (power, fuse, fire); meaningful only where `explosive` is set.
    const QcBlastDefinition &blast(QcBlockId block) const;
    const QcContentRegistry &content() const { return m_content; }

private:
    const QcContentRegistry &m_content;
    std::vector<Entry> m_entries;
};

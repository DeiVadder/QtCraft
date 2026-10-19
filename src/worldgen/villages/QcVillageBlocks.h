#pragma once

#include "common/content/registry/QcContentRegistry.h"

#include <QString>

#include <vector>

// The block states a village counts (decision E-403): bed heads (blocks with `bed`, part head),
// workstations (QcVillageSettings::workstations, every state of the block) and the meeting point
// block (QcVillageSettings::bell). Shared by the generator's styles and the server's village state.
class QcVillageBlocks
{
public:
    // Unknown workstation blocks are left out with a warning (warnings()).
    explicit QcVillageBlocks(const QcContentRegistry &content);

    const QStringList &warnings() const { return m_warnings; }

    bool isBedHead(QcBlockId block) const { return block < m_bedHead.size() && m_bedHead[block]; }
    bool isBell(QcBlockId block) const { return block < m_bell.size() && m_bell[block]; }
    // The kind of work of a workstation state; empty for other blocks.
    QString workstationKind(QcBlockId block) const { return block < m_kind.size() ? m_kind[block] : QString(); }
    // Whether the village state cares about the block at all.
    bool matters(QcBlockId block) const { return isBedHead(block) || !workstationKind(block).isEmpty(); }

private:
    std::vector<bool> m_bedHead;
    std::vector<bool> m_bell;
    std::vector<QString> m_kind;
    QStringList m_warnings;
};

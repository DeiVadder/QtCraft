// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"

#include <QString>

#include <vector>

/*! \brief The block states a village counts (decision E-403): bed heads (blocks with \c {bed}, part
    head), workstations (QcVillageSettings::workstations, every state of the block) and the meeting
    point block (QcVillageSettings::bell).

    Shared by the generator's styles and the server's village state. */
class QcVillageBlocks
{
public:
    /*! Reads the states of \a content and its village settings. Unknown workstation blocks are
        left out with a warning (warnings()). */
    explicit QcVillageBlocks(const QcContentRegistry &content);

    /*! Returns the warnings of the construction (unknown workstation blocks). */
    const QStringList &warnings() const { return m_warnings; }

    /*! Returns whether \a block is the head part of a bed. */
    bool isBedHead(QcBlockId block) const { return block < m_bedHead.size() && m_bedHead[block]; }
    /*! Returns whether \a block is a state of the meeting point block. */
    bool isBell(QcBlockId block) const { return block < m_bell.size() && m_bell[block]; }
    /*! Returns the kind of work of the workstation state \a block; empty for other blocks. */
    QString workstationKind(QcBlockId block) const { return block < m_kind.size() ? m_kind[block] : QString(); }
    /*! Returns whether the village state cares about \a block at all: a bed head or a
        workstation (the meeting point block does not count). */
    bool matters(QcBlockId block) const { return isBedHead(block) || !workstationKind(block).isEmpty(); }

private:
    std::vector<bool> m_bedHead;
    std::vector<bool> m_bell;
    std::vector<QString> m_kind;
    QStringList m_warnings;
};

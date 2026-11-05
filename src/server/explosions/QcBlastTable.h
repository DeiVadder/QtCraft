// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"

#include <vector>

class QcContentRegistry;

/*! \brief What explosions need to know about every block state of a content (decision E-600),
    looked up once: how much of a blast's force it soaks up, whether it stops the sight of a
    blast on bodies (solid blocks do) and whether it is an explosive that a blast lights instead
    of breaking. */
class QcBlastTable
{
public:
    /*! \brief What explosions know about one block state. */
    struct Entry
    {
        /*! How much of a blast's force it soaks up. */
        float resistance = 0.0f;
        /*! It stops the sight of a blast on bodies. */
        bool solid = false;
        /*! A blast lights it instead of breaking it. */
        bool explosive = false;
    };

    explicit QcBlastTable(const QcContentRegistry &content);

    /*! Returns the entry of \a block. */
    const Entry &of(QcBlockId block) const;
    /*! Returns the explosive data of \a block (power, fuse, fire); meaningful only where
        \c {explosive} is set. */
    const QcBlastDefinition &blast(QcBlockId block) const;
    /*! Returns the content the table was read from. */
    const QcContentRegistry &content() const { return m_content; }

private:
    const QcContentRegistry &m_content;
    std::vector<Entry> m_entries;
};

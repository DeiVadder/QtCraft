// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

#include <map>
#include <optional>

// Hands out ranges of a fixed capacity in whole units (first fit) and takes them back, merging free
// neighbours, so one GPU buffer can hold the data of many sections (QcSectionArena, decision E-820).
// The units are the caller's: vertices or 4-byte index words.
class QcRangeAllocator
{
public:
    explicit QcRangeAllocator(quint32 capacity);

    // The first unit of a free range of `count` units, taken from the lowest free range that fits;
    // nothing when no free range is large enough (or `count` is 0).
    [[nodiscard]] std::optional<quint32> allocate(quint32 count);
    // Gives back a range handed out by allocate(), whole; a range outside the capacity or partly
    // free already is reported (lcRenderer) and ignored.
    void release(quint32 start, quint32 count);

    quint32 capacity() const { return m_capacity; }
    quint32 used() const { return m_used; }
    bool isEmpty() const { return m_used == 0; }

private:
    quint32 m_capacity = 0;
    quint32 m_used = 0;
    // Free ranges by first unit, with their length; neighbours are always merged.
    std::map<quint32, quint32> m_free;
};

// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/hanging/QcHangingManager.h"

#include <algorithm>

QcHangingEntity *QcHangingManager::find(quint32 id)
{
    const auto it = m_position.constFind(id);
    return it == m_position.cend() ? nullptr : &m_all[*it];
}

const QcHangingEntity *QcHangingManager::find(quint32 id) const
{
    const auto it = m_position.constFind(id);
    return it == m_position.cend() ? nullptr : &m_all.at(*it);
}

const QcHangingEntity *QcHangingManager::at(QcBlockPos cell, QcBlockFace face) const
{
    const QList<quint32> ids = m_byBlock.value(cell);
    for (const quint32 id : ids) {
        const QcHangingEntity *hanging = find(id);
        if (hanging && hanging->hanging.face == face && hanging->hanging.cells().contains(cell)) {
            return hanging;
        }
    }
    return nullptr;
}

bool QcHangingManager::add(const QcHangingEntity &hanging)
{
    if (m_all.size() >= m_limit) {
        return false;
    }
    m_position.insert(hanging.id, m_all.size());
    m_all.append(hanging);
    index(hanging);
    return true;
}

std::optional<QcHangingEntity> QcHangingManager::remove(quint32 id)
{
    const auto it = m_position.constFind(id);
    if (it == m_position.cend()) {
        return std::nullopt;
    }

    QcHangingEntity removed = m_all.takeAt(*it);
    unindex(removed);
    renumber();
    return removed;
}

qsizetype QcHangingManager::adopt(const QList<QcHangingEntity> &hangings)
{
    qsizetype left = 0;
    for (QcHangingEntity hanging : hangings) {
        if (m_all.size() >= m_limit) {
            ++left;
            continue;
        }
        hanging.settled = false;
        m_position.insert(hanging.id, m_all.size());
        m_all.append(hanging);
        index(hanging);
    }
    return left;
}

void QcHangingManager::removeIf(const std::function<bool(const QcEntity &)> &matches)
{
    for (const QcHangingEntity &hanging : std::as_const(m_all)) {
        if (matches(hanging)) {
            unindex(hanging);
        }
    }
    if (m_all.removeIf([&matches](const QcHangingEntity &hanging) { return matches(hanging); }) > 0) {
        renumber();
    }
}

void QcHangingManager::renumber()
{
    m_position.clear();
    for (qsizetype i = 0; i < m_all.size(); ++i) {
        m_position.insert(m_all.at(i).id, i);
    }
}

void QcHangingManager::noteBlockChange(QcBlockPos pos)
{
    const auto it = m_byBlock.constFind(pos);
    if (it == m_byBlock.cend()) {
        return;
    }
    for (const quint32 id : *it) {
        m_changed.insert(id);
    }
}

QList<quint32> QcHangingManager::takeDueChecks()
{
    QList<quint32> due(m_changed.begin(), m_changed.end());
    m_changed.clear();

    // A slice of the round: every one comes up once in supportCheckTicks ticks however many there are.
    const qsizetype slice = (m_all.size() + supportCheckTicks - 1) / supportCheckTicks;
    for (qsizetype i = 0; i < slice; ++i) {
        if (m_roundNext >= m_all.size()) {
            m_roundNext = 0;
        }
        due.append(m_all.at(m_roundNext).id);
        ++m_roundNext;
    }
    return due;
}

QList<QcBlockPos> QcHangingManager::touchedBlocks(const QcHangingEntity &hanging)
{
    QList<QcBlockPos> blocks;
    const QList<QcBlockPos> cells = hanging.hanging.cells();
    for (const QcBlockPos cell : cells) {
        blocks.append(cell);
        blocks.append(hanging.hanging.supportOf(cell));
    }
    return blocks;
}

void QcHangingManager::index(const QcHangingEntity &hanging)
{
    const QList<QcBlockPos> touched = touchedBlocks(hanging);
    for (const QcBlockPos pos : touched) {
        m_byBlock[pos].append(hanging.id);
    }
}

void QcHangingManager::unindex(const QcHangingEntity &hanging)
{
    const QList<QcBlockPos> touched = touchedBlocks(hanging);
    for (const QcBlockPos pos : touched) {
        const auto it = m_byBlock.find(pos);
        if (it == m_byBlock.end()) {
            continue;
        }
        it->removeOne(hanging.id);
        if (it->isEmpty()) {
            m_byBlock.erase(it);
        }
    }
}

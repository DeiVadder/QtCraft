// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/control/QcControlCommandTable.h"

QcControlCommandTable::QcControlCommandTable(QcControlContext &context)
    : m_session(context)
    , m_movement(context)
    , m_world(context)
    , m_entities(context)
    , m_inventory(context)
    , m_machines(context)
    , m_trades(context)
    , m_gamepads(context)
    , m_admin(context)
{
    m_session.registerCommands(*this);
    m_movement.registerCommands(*this);
    m_world.registerCommands(*this);
    m_entities.registerCommands(*this);
    m_inventory.registerCommands(*this);
    m_machines.registerCommands(*this);
    m_trades.registerCommands(*this);
    m_gamepads.registerCommands(*this);
    m_admin.registerCommands(*this);
}

void QcControlCommandTable::add(const QString &name, const Handler &handler)
{
    Q_ASSERT(!m_handlers.contains(name));
    m_handlers.insert(name, handler);
}

const QcControlCommandTable::Handler *QcControlCommandTable::find(const QString &name) const
{
    const auto it = m_handlers.constFind(name);
    return it == m_handlers.constEnd() ? nullptr : &it.value();
}

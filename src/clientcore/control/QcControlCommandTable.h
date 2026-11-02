// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/control/QcControlAdminCommands.h"
#include "clientcore/control/QcControlEntityCommands.h"
#include "clientcore/control/QcControlGamepadCommands.h"
#include "clientcore/control/QcControlInventoryCommands.h"
#include "clientcore/control/QcControlMachineCommands.h"
#include "clientcore/control/QcControlMovementCommands.h"
#include "clientcore/control/QcControlSessionCommands.h"
#include "clientcore/control/QcControlTradeCommands.h"
#include "clientcore/control/QcControlWorldCommands.h"

#include <QHash>
#include <QJsonObject>
#include <QString>

#include <functional>

class QcControlContext;

/*! \brief The control commands by name, each served by one of the command groups. */
class QcControlCommandTable
{
public:
    /*! Serves one request of the command. */
    using Handler = std::function<void(const QJsonObject &request)>;

    explicit QcControlCommandTable(QcControlContext &context);
    Q_DISABLE_COPY_MOVE(QcControlCommandTable)
    ~QcControlCommandTable() = default;

    /*! Adds the command \a name, served by \a handler. */
    void add(const QString &name, const Handler &handler);
    /*! Returns the handler of the command \a name; nullptr for an unknown command. */
    const Handler *find(const QString &name) const;
    /*! Returns the virtual gamepads of the gamepad commands (decision E-540). */
    QcVirtualGamepadSource &virtualGamepads() { return m_gamepads.source(); }

private:
    QHash<QString, Handler> m_handlers;
    QcControlSessionCommands m_session;
    QcControlMovementCommands m_movement;
    QcControlWorldCommands m_world;
    QcControlEntityCommands m_entities;
    QcControlInventoryCommands m_inventory;
    QcControlMachineCommands m_machines;
    QcControlTradeCommands m_trades;
    QcControlGamepadCommands m_gamepads;
    QcControlAdminCommands m_admin;
};

// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcItemStack.h"

#include <QJsonObject>

class QcClientSession;
class QcControlCommandTable;
class QcControlContext;

// Control commands on machine and chest screens: opening, filling, emptying, closing and their state.
class QcControlMachineCommands
{
public:
    explicit QcControlMachineCommands(QcControlContext &context);

    void registerCommands(QcControlCommandTable &table);

private:
    QcClientSession &session() const;

    void machine(const QJsonObject &request);
    void openMachine(const QJsonObject &request);
    void fillMachine(const QJsonObject &request);
    void takeOutput(const QJsonObject &request);
    void closeMachine(const QJsonObject &request);
    // Moves items between the inventory and the open machine and answers with the machine state.
    void moveMachineItem(const QJsonObject &request, int from, int to);
    // Inventory slot that takes the machine's output: a stack of the same item with room first, else an empty slot; -1 if none.
    int outputDestination(const QcItemStack &output) const;

    QcControlContext &m_context;
};

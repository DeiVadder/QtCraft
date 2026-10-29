// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonObject>

class QcClientSession;
class QcControlCommandTable;
class QcControlContext;

// Control commands on the inventory and crafting: hotbar selection, slot moves, dropping and the recipe book.
class QcControlInventoryCommands
{
public:
    explicit QcControlInventoryCommands(QcControlContext &context);

    void registerCommands(QcControlCommandTable &table);

private:
    QcClientSession &session() const;

    void select(const QJsonObject &request);
    void inventory(const QJsonObject &request);
    void moveItem(const QJsonObject &request);
    void drop(const QJsonObject &request);
    void recipes(const QJsonObject &request);
    void craft(const QJsonObject &request);
    void takeResult(const QJsonObject &request);
    // {"cmd":"creativeItem","item":"stone","slot":n,"count":n}: a stack from the creative catalogue
    // (decision E-283); without a slot into the inventory, without an item the slot is cleared.
    void creativeItem(const QJsonObject &request);
    // {"cmd":"pickBlock"} on the targeted block, or with x, y, z after looking at it (decision E-285).
    void pickBlock(const QJsonObject &request);
    void closeCrafting(const QJsonObject &request);

    QcControlContext &m_context;
};

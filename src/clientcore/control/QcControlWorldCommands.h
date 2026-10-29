// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonObject>

class QcClientSession;
class QcControlCommandTable;
class QcControlContext;

// Control commands that query blocks and act on them: block queries, mining, using the held item on a
// block and lighting fire.
class QcControlWorldCommands
{
public:
    explicit QcControlWorldCommands(QcControlContext &context);

    void registerCommands(QcControlCommandTable &table);

private:
    QcClientSession &session() const;

    void block(const QJsonObject &request);
    void blocks(const QJsonObject &request);
    void mine(const QJsonObject &request);
    // `place` and `use`.
    void useOnBlock(const QJsonObject &request);
    // Selects the first item of the hotbar that lights fire (flint and steel) and uses it like `use`.
    void ignite(const QJsonObject &request);
    // Draws the first launcher of the hotbar (a bow) along yaw/pitch for `ticks` and lets go
    // (decision E-425).
    void shoot(const QJsonObject &request);

    QcControlContext &m_context;
};

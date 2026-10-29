// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"
#include "server/commands/QcCommandRegistry.h"

#include <optional>

class QcServerState;

// /limits (decisions E-349, E-848): shows the simulation limits (QcServerLimits) and changes one,
// within its range, or resets it to its default. The change applies at once. Where it is kept
// follows from where the limits come from: a dedicated server writes it into the "limits" of its
// server.json at once (QcServerConfig::settingsFile; the other keys of the file stay as they are),
// a single-player world keeps it in world.json ("limits", only the keys an operator set), written
// with the next save. The admin channel's settings change limits through this command.
class QcLimitCommands
{
public:
    explicit QcLimitCommands(QcServerState &state);

    void addTo(QcCommandRegistry &registry);

private:
    void limits(const QcCommandContext &context);
    void list(const QcCommandContext &context) const;
    // set <key> <value> or reset <key>.
    void change(const QcCommandContext &context);
    // Keeps the limit's new value (std::nullopt: its default) where the limits come from; the
    // reply names the place.
    [[nodiscard]] QcStatus store(const QString &key, std::optional<qint64> value);

    QcServerState &m_state;
};

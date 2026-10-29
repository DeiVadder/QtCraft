// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcAdventureRules.h"
#include "server/commands/QcCommandRegistry.h"

#include <optional>

class QcServerGameModes;
class QcServerState;

// /adventure (decision E-735), for operators: shows and changes the world's allow-lists of
// adventure mode, which item kinds may break which blocks (canBreak) and act on which blocks
// (canPlaceOn). Items and blocks are checked against the content; "*" stands for every item and
// the empty hand. Adventure players get the changed lists at once.
class QcAdventureCommands
{
public:
    QcAdventureCommands(QcServerState &state, QcServerGameModes &gameModes);

    void addTo(QcCommandRegistry &registry);

private:
    void adventure(const QcCommandContext &context);
    void list(const QcCommandContext &context) const;
    // /adventure <canBreak|canPlaceOn> <item|*> <block>…
    void allow(const QcCommandContext &context, QcAdventureRules::Action action);
    // /adventure remove <canBreak|canPlaceOn> <item|*> [block…]
    void remove(const QcCommandContext &context);
    void clear(const QcCommandContext &context);

    // The word as a listed name: "*" as it is, anything else qualified (not checked against content).
    [[nodiscard]] static QString listedName(const QString &word);
    // The qualified item name or "*"; nullopt (after a reply) for an unknown item.
    [[nodiscard]] std::optional<QString> itemName(const QcCommandContext &context, const QString &word) const;
    // The qualified block names of `words[first..]`; nullopt (after a reply) when one is unknown.
    [[nodiscard]] std::optional<QStringList> blockNames(const QcCommandContext &context, qsizetype first) const;

    QcServerState &m_state;
    QcServerGameModes &m_gameModes;
};

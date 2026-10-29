// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/admin/QcAdminLineModel.h"
#include "common/protocol/QcAdminMessages.h"

#include <QVariantList>
#include <QVariantMap>

// What the "Manage server" screen shows of the admin channel's reports (decision E-863), as values
// for QML: pure conversions of the messages, translated where the server sends translation keys
// (rules in the context "QcGameRules", limits in "QcServerLimits", commands in "QcServerMessages").
namespace QcAdminViews {

// The status: numbers as they came (QML formats them for the player's language) and texts.
// Keys: tps, tickMean, tickP99, tickMax, players, chunks, entities, memory, peakMemory, sent,
// received, uptimeMs, tick, skipped, world, seed, generator, version, protocol, phases ([{name,
// ms}], the slowest first, at most five).
QVariantMap status(const QcMsg::AdminStatus &status);
// The history as one list of numbers per status key (tps, tickMean, tickP99, players, chunks,
// entities, memory, sent, received), oldest first: `minutes` the day, else the last ten minutes.
QVariantMap charts(const QcMsg::AdminHistory &history, bool minutes);
// [{name, ping, gameMode (QcGameMode), modeName (translated), operator, local, address,
// connectedMs}], sorted by name.
QVariantList players(const QList<QcMsg::AdminPlayers::Entry> &players);
// {whitelistEnabled, operators, whitelist, bans: [{name, reason, by, sinceMs}]}, names sorted.
QVariantMap lists(const QcMsg::AdminLists &lists);
// The settings in their groups, in the server's order: [{title, settings: [{key, name, named,
// limit, description, boolean, low, high, defaultValue, value, lowText, highText, defaultText}]}].
// The name is translated (QcAdminSettingNames; the key where it has none, then `named` is false);
// the key is the name /gamerule and /limits take and shows beside it. The texts are the numbers in
// the player's language.
QVariantList groups(const QList<QcMsg::AdminSettings::Setting> &settings, bool limits);

// Help for the command `line` begins with: {command, usage, description (translated; macros as
// they are), next (the parameters the next word may be, "<player>", "[reason]", "on|off", joined
// with " / ")}; empty when the first word names no command of `catalogue`.
QVariantMap help(const QString &line, const QcMsg::AdminCatalogue &catalogue);

QList<QcAdminLineModel::Line> chatLines(const QList<QcMsg::AdminChatLine> &chat);
QList<QcAdminLineModel::Line> logLines(const QList<QcMsg::AdminLogLine> &log);

} // namespace QcAdminViews

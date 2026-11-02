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

// The status: numbers as they came and texts. Keys: measured (true), tps, tickMean, tickP99,
// tickMax, players, chunks, entities, memory, peakMemory, sent, received, uptimeMs, tick, skipped,
// world, seed, generator, version, protocol, phases ([{name, ms, msText}], the slowest first, at
// most five), and the numbers as the overview shows them in the player's language: tpsText,
// tickText (99th percentile and mean), playersText, chunksText, entitiesText, memoryText,
// sentText, receivedText, peakMemoryText, skippedText, uptimeText (days, hours, minutes),
// versionText (with the protocol).
QVariantMap status(const QcMsg::AdminStatus &status);
// The status before the first one came: measured false, every text, the world, the seed and the
// generator "–" instead of a zero (AD-5 of docs/Menue-Pruefung.md), no phases.
QVariantMap emptyStatus();
// The history as one list of numbers per status key (tps, tickMean, tickP99, players, chunks,
// entities, memory, sent, received), oldest first: `minutes` the day, else the last ten minutes.
QVariantMap charts(const QcMsg::AdminHistory &history, bool minutes);
// [{name, ping, gameMode (QcGameMode), modeName (translated), operator, local, address,
// connectedMs, details, own}], sorted by name: `details` the line under the name (ping, game mode,
// address; the host only his game mode), `own` the row of the player named `ownName` (empty: none).
QVariantList players(const QList<QcMsg::AdminPlayers::Entry> &players, const QString &ownName);
// `name` and `other` name the same player (as the server compares names, decision E-931).
bool sameName(const QString &name, const QString &other);
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

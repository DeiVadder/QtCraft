// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

// The names the "Manage server" screen shows for game rules and server limits (decision E-863),
// translated in the context "QcAdminSettingNames"; the key (the name /gamerule and /limits take)
// stays visible beside it. The server sends only keys and descriptions, so a key from a newer
// server or a mod has no name here and shows as itself.
namespace QcAdminSettingNames {

// The translated name of the rule (`limit` false) or limit `key`; empty when it has none.
QString name(const QString &key, bool limit);

} // namespace QcAdminSettingNames

// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>

#include <utility>

class QcContentRegistry;

// Checks the links between Signal Dust blocks once all packs are loaded (decision E-333): a piston's
// `head` must be a `piston_head` block with the piston's `facing` values and `sticky`; a torch's or
// lamp's `counterpart` must exist, be of the same kind, have the opposite `active` and the same
// properties. Problems come as (block name, message); the registry switches such blocks' circuit
// part off with a warning.
namespace QcSignalLinks {

QList<std::pair<QString, QString>> problems(const QcContentRegistry &content);

} // namespace QcSignalLinks

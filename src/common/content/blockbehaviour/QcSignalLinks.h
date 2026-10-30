// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>

#include <utility>

class QcContentRegistry;

/*! \brief Checks the links between Signal Dust blocks once all packs are loaded (decision E-333): a
    piston's \c {head} must be a \c {piston_head} block with the piston's \c {facing} values and
    \c {sticky}; a torch's or lamp's \c {counterpart} must exist, be of the same kind, have the
    opposite \c {active} and the same properties.

    Problems come as (block name, message); the registry switches such blocks' circuit part off
    with a warning. */
namespace QcSignalLinks {

/*! Returns the problems of the links between the Signal Dust blocks of \a content, as
    (block name, message). */
QList<std::pair<QString, QString>> problems(const QcContentRegistry &content);

} // namespace QcSignalLinks

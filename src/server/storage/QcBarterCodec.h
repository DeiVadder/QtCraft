// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/animals/QcBarterState.h"

#include <QDataStream>

#include <optional>

class QcContentRegistry;
class QcStackReader;

/*! \brief The barter component of a creature entry (QcEntityCodec::Component::Barter, decision
    E-1081): the item it looks at and the seconds left, and the kept stacks, each stack as every
    save file holds it (QcSaveFile::writeStack, QcStackReader). */
namespace QcBarterCodec {

/*! Returns the component read from \a in with \a stacks, which drops an item the content no
    longer has with a warning; std::nullopt for damaged counts. */
[[nodiscard]] std::optional<QcBarterState> read(QDataStream &in, QcStackReader &stacks);

/*! Writes \a barter to \a out, items by their names in \a content; what QcBarterState::saved()
    leaves out is not saved. */
void write(QDataStream &out, const QcBarterState &barter, const QcContentRegistry &content);

} // namespace QcBarterCodec

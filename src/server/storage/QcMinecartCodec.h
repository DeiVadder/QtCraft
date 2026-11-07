// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcItemStack.h"

#include <QDataStream>
#include <QList>
#include <QString>

#include <optional>

class QcContentRegistry;
class QcStackReader;
struct QcMinecart;

/*! \brief The minecart component of an entity entry (QcEntityCodec::Component::Minecart, decision
    E-386): the cart's item, a container cart's slots, each stack as every save file holds it
    (QcSaveFile::writeStack, QcStackReader), and whether a hopper cart pulls. */
namespace QcMinecartCodec {

/*! \brief The component as read. \qcfields
    \c {rest} holds what lay beyond a stack's limit and found no empty slot (decision E-1066). */
struct Fields
{
    QString item;
    QList<QcItemStack> stacks;
    QList<QcItemStack> rest;
    bool enabled = true;
};

/*! Returns the component read from \a in with \a stacks, which collects overflow from here on;
    std::nullopt for a damaged slot count. */
[[nodiscard]] std::optional<Fields> read(QDataStream &in, QcStackReader &stacks);

/*! Writes the component of \a cart to \a out, items by their names in \a content. */
void write(QDataStream &out, const QcMinecart &cart, const QcContentRegistry &content);

} // namespace QcMinecartCodec

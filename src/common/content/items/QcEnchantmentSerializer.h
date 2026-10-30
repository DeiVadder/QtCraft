// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcEnchantmentDefinition.h"
#include "common/core/QcResult.h"

#include <QDataStream>
#include <QList>

class QcContentRegistry;

/*! \brief The enchantments in the content sync (QcContentSerializer, decision E-864): every field,
    so the client knows effects and names.

    Reading checks every value like the parser. */
namespace QcEnchantmentSerializer {

/*! Writes the count of \a enchantments and then each of them to \a stream. */
void write(QDataStream &stream, const QList<QcEnchantmentDefinition> &enchantments);
/*! Reads the enchantments written by write() from \a stream and registers each in \a registry;
    returns an error at the first corrupt or implausible one. */
[[nodiscard]] QcStatus read(QDataStream &stream, QcContentRegistry &registry);

} // namespace QcEnchantmentSerializer

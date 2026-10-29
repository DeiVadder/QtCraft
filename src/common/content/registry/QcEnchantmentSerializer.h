// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcEnchantmentDefinition.h"
#include "common/core/QcResult.h"

#include <QDataStream>
#include <QList>

class QcContentRegistry;

// The enchantments in the content sync (QcContentSerializer, decision E-864): every field, so the
// client knows effects and names. Reading checks every value like the parser.
namespace QcEnchantmentSerializer {

void write(QDataStream &stream, const QList<QcEnchantmentDefinition> &enchantments);
[[nodiscard]] QcStatus read(QDataStream &stream, QcContentRegistry &registry);

} // namespace QcEnchantmentSerializer

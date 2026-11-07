// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/storage/QcMinecartCodec.h"

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/protocol/QcStreamLimits.h"
#include "server/entities/QcMinecart.h"
#include "server/storage/QcSaveFile.h"
#include "server/storage/QcStackReader.h"

#include <utility>

namespace QcMinecartCodec {

std::optional<Fields> read(QDataStream &in, QcStackReader &stacks)
{
    Fields fields;
    in >> fields.item;
    const std::optional<quint32> slotCount = QcStreamLimits::readCount(in, QcBlockDefinition::maxContainerSlots);
    if (!slotCount) {
        return std::nullopt;
    }

    stacks.setCollectsOverflow(true);
    QList<QcItemStack> held(*slotCount);
    for (QcItemStack &stack : held) {
        stack = stacks.read(in);
    }
    QcStackReader::Filled filled = stacks.fillWithOverflow(std::move(held));
    fields.stacks = std::move(filled.stacks);
    fields.rest = std::move(filled.rest);
    in >> fields.enabled;
    return fields;
}

void write(QDataStream &out, const QcMinecart &cart, const QcContentRegistry &content)
{
    out << cart.item << static_cast<quint32>(cart.stacks.size());
    for (const QcItemStack &stack : cart.stacks) {
        QcSaveFile::writeStack(out, stack, content);
    }
    out << cart.enabled;
}

} // namespace QcMinecartCodec

// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Payloads of the machine messages (QcMachineMessages.h): furnace, chest and stonecutter screens.

#include "common/protocol/QcProtocolCodec.h"

#include "common/content/registry/QcStonecutting.h"
#include "common/gameplay/QcInventory.h"
#include "common/protocol/QcStreamLimits.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace QcProtocolCodec {

void encode(QDataStream &stream, const QcMsg::OpenMachine &m)
{
    writePos(stream, m.pos);
    stream << m.category << m.entity << m.heldSlot << m.name;
}

void encode(QDataStream &stream, const QcMsg::MachineState &m)
{
    writePos(stream, m.pos);
    stream << m.entity;
    writeStacks(stream, m.stacks);
    stream << m.burn << m.progress;
}

void encode(QDataStream &, QcMsg::CloseMachine) {}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::OpenMachine>, QDataStream &stream)
{
    QcMsg::OpenMachine m;
    m.pos = readPos(stream);
    stream >> m.category >> m.entity >> m.heldSlot >> m.name;
    if (m.category.size() > 64) {
        return qcError(u"Invalid machine category"_s);
    }
    if (m.name.size() > QcItemData::maxNameLength) {
        return qcError(u"Invalid machine name"_s);
    }
    // A held backpack is in the hotbar and opens as a storage screen of its own (no cart).
    if (m.heldSlot != QcMsg::OpenMachine::noHeldSlot
        && (m.heldSlot < 0 || m.heldSlot >= QcInventory::hotbarSize || !m.category.isEmpty() || m.entity != 0)) {
        return qcError(u"Invalid held container slot"_s);
    }
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::MachineState>, QDataStream &stream)
{
    QcMsg::MachineState m;
    m.pos = readPos(stream);
    stream >> m.entity;
    const std::optional<quint32> countRead = QcStreamLimits::readCount(stream, QcMsg::MachineState::maxSlots);
    if (!countRead || *countRead == 0) {
        return qcError(u"Corrupt machine state"_s);
    }
    const quint32 count = *countRead;

    for (quint32 i = 0; i < count; ++i) {
        m.stacks.append(readStack(stream));
    }

    stream >> m.burn >> m.progress;
    if (!std::isfinite(m.burn) || !std::isfinite(m.progress)) {
        return qcError(u"Non-finite machine state"_s);
    }

    m.burn = std::clamp(m.burn, 0.0f, 1.0f);
    m.progress = std::clamp(m.progress, 0.0f, 1.0f);
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CloseMachine>, QDataStream &)
{
    return QcMsg::CloseMachine{};
}

void encode(QDataStream &stream, QcMsg::OpenStonecutter m)
{
    writePos(stream, m.pos);
}

void encode(QDataStream &stream, const QcMsg::StonecutterState &m)
{
    writeStack(stream, m.input);
    stream << m.recipe;
}

void encode(QDataStream &stream, const QcMsg::SelectStonecutterRecipe &m)
{
    stream << m.recipe;
}

void encode(QDataStream &, QcMsg::CloseStonecutter) {}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::OpenStonecutter>, QDataStream &stream)
{
    QcMsg::OpenStonecutter m;
    m.pos = readPos(stream);
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::StonecutterState>, QDataStream &stream)
{
    QcMsg::StonecutterState m;
    m.input = readStack(stream);
    stream >> m.recipe;
    if (m.recipe.size() > QcStonecutting::maxRecipeName) {
        return qcError(u"Invalid stonecutting recipe"_s);
    }
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::SelectStonecutterRecipe>, QDataStream &stream)
{
    QcMsg::SelectStonecutterRecipe m;
    stream >> m.recipe;
    if (m.recipe.size() > QcStonecutting::maxRecipeName) {
        return qcError(u"Invalid stonecutting recipe"_s);
    }
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CloseStonecutter>, QDataStream &)
{
    return QcMsg::CloseStonecutter{};
}

void encode(QDataStream &stream, QcMsg::OpenEnchanting m)
{
    writePos(stream, m.pos);
}

void encode(QDataStream &stream, const QcMsg::EnchantingState &m)
{
    writeStack(stream, m.item);
    writeStack(stream, m.lapis);
    stream << m.shelves;
    for (const QcMsg::EnchantingOffer &offer : m.offers) {
        stream << offer.levelCost << offer.clue << offer.clueLevel;
    }
    stream << m.runeSeed;
}

void encode(QDataStream &stream, QcMsg::ChooseEnchantment m)
{
    stream << m.slot;
}

void encode(QDataStream &, QcMsg::CloseEnchanting) {}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::OpenEnchanting>, QDataStream &stream)
{
    QcMsg::OpenEnchanting m;
    m.pos = readPos(stream);
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::EnchantingState>, QDataStream &stream)
{
    QcMsg::EnchantingState m;
    m.item = readStack(stream);
    m.lapis = readStack(stream);
    stream >> m.shelves;
    for (QcMsg::EnchantingOffer &offer : m.offers) {
        stream >> offer.levelCost >> offer.clue >> offer.clueLevel;
        if (offer.levelCost < 0 || offer.clue.size() > QcMsg::EnchantingState::maxClue || offer.clueLevel < 0 || offer.clueLevel > 255) {
            return qcError(u"Invalid enchanting offer"_s);
        }
    }
    stream >> m.runeSeed;
    if (m.shelves < 0 || m.shelves > 15) {
        return qcError(u"Invalid bookshelf count"_s);
    }
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ChooseEnchantment>, QDataStream &stream)
{
    QcMsg::ChooseEnchantment m;
    stream >> m.slot;
    if (m.slot < 0 || m.slot > 2) {
        return qcError(u"Invalid enchanting offer"_s);
    }
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CloseEnchanting>, QDataStream &)
{
    return QcMsg::CloseEnchanting{};
}

void encode(QDataStream &stream, QcMsg::OpenAnvil m)
{
    writePos(stream, m.pos);
}

void encode(QDataStream &stream, const QcMsg::AnvilState &m)
{
    writeStack(stream, m.left);
    writeStack(stream, m.right);
    stream << m.name;
    writeStack(stream, m.output);
    stream << m.levelCost;
}

void encode(QDataStream &stream, const QcMsg::RenameAnvil &m)
{
    stream << m.name;
}

void encode(QDataStream &stream, const QcMsg::ForgeAnvil &m)
{
    stream << m.levelCost;
    writeStack(stream, m.output);
}

void encode(QDataStream &, QcMsg::CloseAnvil) {}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::OpenAnvil>, QDataStream &stream)
{
    QcMsg::OpenAnvil m;
    m.pos = readPos(stream);
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AnvilState>, QDataStream &stream)
{
    QcMsg::AnvilState m;
    m.left = readStack(stream);
    m.right = readStack(stream);
    stream >> m.name;
    m.output = readStack(stream);
    stream >> m.levelCost;
    if (m.name.size() > QcMsg::RenameAnvil::maxText || m.levelCost < 0) {
        return qcError(u"Invalid anvil state"_s);
    }
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::RenameAnvil>, QDataStream &stream)
{
    QcMsg::RenameAnvil m;
    stream >> m.name;
    if (m.name.size() > QcMsg::RenameAnvil::maxText) {
        return qcError(u"Anvil name too long"_s);
    }
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ForgeAnvil>, QDataStream &stream)
{
    QcMsg::ForgeAnvil m;
    stream >> m.levelCost;
    m.output = readStack(stream);
    if (m.levelCost < 0) {
        return qcError(u"Invalid anvil cost"_s);
    }
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CloseAnvil>, QDataStream &)
{
    return QcMsg::CloseAnvil{};
}

} // namespace QcProtocolCodec

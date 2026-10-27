#pragma once

#include "common/protocol/QcMessages.h"

#include <optional>

// What a raw protocol client (QcRawClient) recorded about the open workstation screens: the
// stonecutter (decision E-785), the enchanting table (decision E-864) and the anvil (decision E-866).
struct QcRawWorkstations
{
    // Opens, states and closes of the workstations; false for another message.
    bool receivedWorkstation(const QcMessage &message)
    {
        if (const auto *cutter = std::get_if<QcMsg::OpenStonecutter>(&message)) {
            stonecutter = cutter->pos;
        } else if (const auto *cutterState = std::get_if<QcMsg::StonecutterState>(&message)) {
            stonecutterState = *cutterState;
        } else if (std::holds_alternative<QcMsg::CloseStonecutter>(message)) {
            stonecutter.reset();
            stonecutterState = {};
            stonecutterClosed = true;
        } else if (const auto *table = std::get_if<QcMsg::OpenEnchanting>(&message)) {
            enchanting = table->pos;
        } else if (const auto *tableState = std::get_if<QcMsg::EnchantingState>(&message)) {
            enchantingState = *tableState;
        } else if (std::holds_alternative<QcMsg::CloseEnchanting>(message)) {
            enchanting.reset();
            enchantingState = {};
        } else if (const auto *opened = std::get_if<QcMsg::OpenAnvil>(&message)) {
            anvil = opened->pos;
        } else if (const auto *state = std::get_if<QcMsg::AnvilState>(&message)) {
            anvilState = *state;
        } else if (std::holds_alternative<QcMsg::CloseAnvil>(message)) {
            anvil.reset();
            anvilState = {};
        } else {
            return false;
        }
        return true;
    }

    // The open stonecutter, its slots as last sent and whether the server closed one.
    std::optional<QcBlockPos> stonecutter;
    QcMsg::StonecutterState stonecutterState;
    bool stonecutterClosed = false;
    // The open enchanting table and its state as last sent.
    std::optional<QcBlockPos> enchanting;
    QcMsg::EnchantingState enchantingState;
    // The open anvil and its state as last sent.
    std::optional<QcBlockPos> anvil;
    QcMsg::AnvilState anvilState;
};

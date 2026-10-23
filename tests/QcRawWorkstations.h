#pragma once

#include "common/protocol/QcMessages.h"

#include <optional>

// What a raw protocol client (QcRawClient) recorded about the open workstation screens: the
// stonecutter (decision E-785) and the enchanting table (decision E-864).
struct QcRawWorkstations
{
    // Opens, states and closes of the workstations; false for another message.
    bool receivedWorkstation(const QcMessage &message)
    {
        if (const auto *cutter = std::get_if<QcMsg::OpenStonecutter>(&message)) {
            stonecutter = cutter->pos;
        } else if (const auto *cutter = std::get_if<QcMsg::StonecutterState>(&message)) {
            stonecutterState = *cutter;
        } else if (std::holds_alternative<QcMsg::CloseStonecutter>(message)) {
            stonecutter.reset();
            stonecutterState = {};
            stonecutterClosed = true;
        } else if (const auto *table = std::get_if<QcMsg::OpenEnchanting>(&message)) {
            enchanting = table->pos;
        } else if (const auto *table = std::get_if<QcMsg::EnchantingState>(&message)) {
            enchantingState = *table;
        } else if (std::holds_alternative<QcMsg::CloseEnchanting>(message)) {
            enchanting.reset();
            enchantingState = {};
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
};

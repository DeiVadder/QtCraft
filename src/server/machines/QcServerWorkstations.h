#pragma once

#include "server/machines/QcServerEnchanting.h"
#include "server/machines/QcServerStonecutter.h"

// The block screens with slots of their own that the player fills from the inventory and that
// give their items back on closing: the stonecutter (decision E-785) and the enchanting table
// (decision E-864). At most one of them is open per player.
struct QcServerWorkstations
{
    QcServerWorkstations(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory, QcBlockScreens &screens)
        : stonecutter(state, sounds, inventory, screens)
        , enchanting(state, sounds, inventory, screens)
    {
    }

    // Once per tick: closes the screens that may not stay open.
    void update()
    {
        stonecutter.update();
        enchanting.update();
    }

    // The player left: the slots go back, nothing is sent.
    void leave(QcServerClient &client)
    {
        stonecutter.close(client, false);
        enchanting.close(client, false);
    }

    // Handles the choices and closes of the workstation screens; false for another message.
    bool handleMessage(QcServerClient &client, const QcMessage &message)
    {
        if (const auto *recipe = std::get_if<QcMsg::SelectStonecutterRecipe>(&message)) {
            stonecutter.select(client, *recipe);
        } else if (std::holds_alternative<QcMsg::CloseStonecutter>(message)) {
            stonecutter.close(client, false);
        } else if (const auto *choice = std::get_if<QcMsg::ChooseEnchantment>(&message)) {
            enchanting.choose(client, *choice);
        } else if (std::holds_alternative<QcMsg::CloseEnchanting>(message)) {
            enchanting.close(client, false);
        } else {
            return false;
        }
        return true;
    }

    QcServerStonecutter stonecutter;
    QcServerEnchanting enchanting;
};

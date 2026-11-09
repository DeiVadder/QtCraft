// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/machines/QcServerAnvil.h"
#include "server/machines/QcServerBeacons.h"
#include "server/machines/QcServerEnchanting.h"
#include "server/machines/QcServerSmithing.h"
#include "server/machines/QcServerStonecutter.h"

/*! \brief The block screens with slots of their own that the player fills from the inventory and
    that give their items back on closing: the stonecutter (decision E-785), the enchanting table
    (decision E-864), the anvil (decision E-866), the smithing table (decision E-1115) and the
    beacon (decision E-1142), whose effects also run here.

    At most one of them is open per player. */
struct QcServerWorkstations
{
    QcServerWorkstations(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory, QcBlockScreens &screens, QcServerBlocks &blocks)
        : stonecutter(state, sounds, inventory, screens)
        , enchanting(state, sounds, inventory, screens)
        , anvil(state, sounds, inventory, screens, blocks)
        , smithing(state, sounds, inventory, screens)
        , beacon(state, sounds, inventory, screens)
    {
    }

    /*! Opens the screen of the workstation at \a pos for \a client, if the block there is one.
        Returns \c false for another block. */
    bool open(QcServerClient &client, QcBlockPos pos)
    {
        return stonecutter.open(client, pos) || enchanting.open(client, pos) || anvil.open(client, pos) || smithing.open(client, pos)
               || beacon.open(client, pos);
    }

    /*! Once per tick: closes the screens that may not stay open; the beacons look around on
        their schedule. */
    void update()
    {
        stonecutter.update();
        enchanting.update();
        anvil.update();
        smithing.update();
        beacon.update();
    }

    /*! The player \a client left: the slots go back, nothing is sent. */
    void leave(QcServerClient &client)
    {
        stonecutter.close(client, false);
        enchanting.close(client, false);
        anvil.close(client, false);
        smithing.close(client, false);
        beacon.close(client, false);
    }

    /*! Handles the choices and closes of the workstation screens in \a message of \a client.
        Returns \c false for another message. */
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
        } else if (const auto *name = std::get_if<QcMsg::RenameAnvil>(&message)) {
            anvil.rename(client, *name);
        } else if (const auto *forge = std::get_if<QcMsg::ForgeAnvil>(&message)) {
            anvil.forge(client, *forge);
        } else if (std::holds_alternative<QcMsg::CloseAnvil>(message)) {
            anvil.close(client, false);
        } else if (const auto *smith = std::get_if<QcMsg::ForgeSmithing>(&message)) {
            smithing.forge(client, *smith);
        } else if (std::holds_alternative<QcMsg::CloseSmithing>(message)) {
            smithing.close(client, false);
        } else if (const auto *choice = std::get_if<QcMsg::ChooseBeaconEffects>(&message)) {
            beacon.choose(client, *choice);
        } else if (std::holds_alternative<QcMsg::CloseBeacon>(message)) {
            beacon.close(client, false);
        } else {
            return false;
        }
        return true;
    }

    /*! The stonecutter screens. */
    QcServerStonecutter stonecutter;
    /*! The enchanting table screens. */
    QcServerEnchanting enchanting;
    /*! The anvil screens. */
    QcServerAnvil anvil;
    /*! The smithing table screens. */
    QcServerSmithing smithing;
    /*! The beacons: their screens and effects. */
    QcServerBeacons beacon;
};

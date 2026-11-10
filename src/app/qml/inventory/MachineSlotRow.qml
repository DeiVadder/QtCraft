// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtCraft

// Slots of a workbench or machine screen side by side (decision E-884): `count` rows from `first`
// on of the inventory model, with object names for tests (`names`, one per slot, or none), each
// showing a faint item while empty (`placeholders`, item names; empty for none).
Row {
    id: row

    required property SlotScreen owner
    required property int first
    property list<string> names
    property int count: names.length
    property list<string> placeholders

    spacing: Ui.space.s

    Repeater {
        model: SlotRange {
            source: row.owner.game.player.inventory
            first: row.first
            count: row.count
        }

        InventorySlot {
            required property int index
            objectName: index < row.names.length ? row.names[index] : ""
            owner: row.owner
            placeholderIcon: index < row.placeholders.length && row.placeholders[index].length > 0
                             ? ItemIcons.source(row.placeholders[index], ItemIcons.generation) : ""
        }
    }
}

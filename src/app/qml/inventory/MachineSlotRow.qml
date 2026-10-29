// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtCraft

// Input slots of a workbench screen side by side (decision E-884): rows `first` onwards of the
// inventory model, one per name in `names` (object names for tests), each showing a faint item
// while empty (`placeholders`, item names; empty for none).
Row {
    id: row

    required property SlotScreen owner
    required property int first
    required property list<string> names
    property list<string> placeholders

    spacing: Ui.space.s

    Repeater {
        model: SlotRange {
            source: row.owner.game.player.inventory
            first: row.first
            count: row.names.length
        }

        InventorySlot {
            required property int index
            objectName: row.names[index]
            owner: row.owner
            placeholderIcon: index < row.placeholders.length && row.placeholders[index].length > 0
                             ? ItemIcons.source(row.placeholders[index], ItemIcons.generation) : ""
        }
    }
}

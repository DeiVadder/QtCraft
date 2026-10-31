// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtCraft

// The keyboard's and mouse's hints of a screen as one row of plates (docs/UI-Leitfaden.md 6.10,
// IV-2 and HB-4 of docs/Menue-Pruefung.md): each entry {keys, text} shows its keys as KeyBadge
// plates and what they do ("Click: pick up and put down"), instead of a sentence. While the pad
// leads, PadHints shows its buttons instead. Wraps onto a second line when the row is narrow.
Flow {
    id: row

    // The entries: {keys: ["Shift-click"], text: "move the stack across"}.
    property var entries: []

    spacing: Ui.space.l

    Repeater {
        model: row.entries

        Row {
            id: entry

            required property var modelData

            spacing: Ui.space.xs

            Repeater {
                model: entry.modelData.keys

                KeyBadge {
                    required property string modelData

                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData
                }
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                // Wraps when the entry alone is wider than the row.
                width: row.width > 0 ? Math.min(implicitWidth, Math.max(0, row.width - x)) : implicitWidth
                wrapMode: Text.Wrap
                text: entry.modelData.text
                color: Theme.textSecondary
                font.pixelSize: Ui.type.help
            }
        }
    }
}

// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The character preview with the controls to check a look (decision E-231): front and back view,
// standing, walking and mining, "Reset view" below them as a quiet button (CH-4 of
// docs/Menue-Pruefung.md), and the camera key of the game as a plate (CH-3). Dragging turns the
// figure, the wheel zooms.
ColumnLayout {
    id: panel

    required property GameController game
    // The CharacterPreview while the panel is shown, else null.
    readonly property CharacterPreview preview: host.preview

    spacing: Ui.space.s

    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        radius: Ui.radius
        color: Theme.panelRaised
        border.color: Theme.panelBorder
        border.width: Ui.size.line

        CharacterPreviewHost {
            id: host

            previewName: "characterPreview"
            anchors.fill: parent
            anchors.margins: Ui.size.bevelDeep
            source: panel.game.preview
            paused: panel.game.handbook.isOpen
        }
    }

    Segments {
        Layout.fillWidth: true
        label: qsTr("Side")
        model: [
            { value: CharacterPreview.Side.Front, text: qsTr("Front") },
            { value: CharacterPreview.Side.Back, text: qsTr("Back") }
        ]
        currentValue: panel.preview?.side ?? CharacterPreview.Side.Front
        onActivated: value => panel.preview?.showSide(value)
    }

    Segments {
        Layout.fillWidth: true
        label: qsTr("Motion")
        model: [
            { value: CharacterPreview.Animation.Stand, text: qsTr("Stand") },
            { value: CharacterPreview.Animation.Walk, text: qsTr("Walk") },
            { value: CharacterPreview.Animation.Mine, text: qsTr("Mine") }
        ]
        currentValue: panel.preview?.animation ?? CharacterPreview.Animation.Stand
        onActivated: value => {
            if (panel.preview)
                panel.preview.animation = value;
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Ui.space.s

        Label {
            Layout.fillWidth: true
            text: qsTr("Drag to turn the figure, scroll to zoom.")
            wrapMode: Text.Wrap
            font.pixelSize: Ui.type.help
            color: Theme.textMuted
        }
        GameButton {
            kind: GameButton.Quiet
            text: qsTr("Reset view")
            onClicked: panel.preview?.resetView()
        }
    }

    KeyHintRow {
        Layout.fillWidth: true
        entries: [{ keys: [panel.game.settings.keys.labels.cameraView], text: qsTr("in the game: your character from behind or the front") }]
    }
}

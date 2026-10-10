pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The character preview with the controls to check a look (decision E-231): front and back view,
// standing, walking and mining. Dragging turns the figure, the wheel zooms.
ColumnLayout {
    id: panel

    required property GameController game
    // The CharacterPreview while the panel is shown, else null.
    readonly property CharacterPreview preview: host.preview

    spacing: 6

    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        radius: 4
        color: Theme.panelRaised
        border.color: Theme.panelBorder

        CharacterPreviewHost {
            id: host

            previewName: "characterPreview"
            anchors.fill: parent
            anchors.margins: 2
            source: panel.game.preview
            paused: panel.game.handbook.isOpen
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 4

        Button {
            Layout.fillWidth: true
            text: qsTr("Front")
            highlighted: panel.preview?.side === "front"
            onClicked: panel.preview?.showSide("front")
        }
        Button {
            Layout.fillWidth: true
            text: qsTr("Back")
            highlighted: panel.preview?.side === "back"
            onClicked: panel.preview?.showSide("back")
        }
        Button {
            Layout.fillWidth: true
            text: qsTr("Reset view")
            onClicked: panel.preview?.resetView()
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 4

        Repeater {
            model: [
                { value: "stand", text: qsTr("Stand") },
                { value: "walk", text: qsTr("Walk") },
                { value: "mine", text: qsTr("Mine") }
            ]

            delegate: Button {
                required property var modelData

                Layout.fillWidth: true
                text: modelData.text
                highlighted: panel.preview?.animation === modelData.value
                onClicked: {
                    if (panel.preview)
                        panel.preview.animation = modelData.value;
                }
            }
        }
    }

    Label {
        Layout.fillWidth: true
        text: qsTr("Drag to turn the figure, scroll to zoom.")
        wrapMode: Text.Wrap
        opacity: 0.7
    }
}

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

    spacing: Ui.px(6)

    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        radius: Ui.px(4)
        color: Theme.panelRaised
        border.color: Theme.panelBorder

        CharacterPreviewHost {
            id: host

            previewName: "characterPreview"
            anchors.fill: parent
            anchors.margins: Ui.px(2)
            source: panel.game.preview
            paused: panel.game.handbook.isOpen
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Ui.px(4)

        Button {
            Layout.fillWidth: true
            text: qsTr("Front")
            highlighted: panel.preview?.side === CharacterPreview.Side.Front
            onClicked: panel.preview?.showSide(CharacterPreview.Side.Front)
        }
        Button {
            Layout.fillWidth: true
            text: qsTr("Back")
            highlighted: panel.preview?.side === CharacterPreview.Side.Back
            onClicked: panel.preview?.showSide(CharacterPreview.Side.Back)
        }
        Button {
            Layout.fillWidth: true
            text: qsTr("Reset view")
            onClicked: panel.preview?.resetView()
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Ui.px(4)

        Repeater {
            model: [
                { value: CharacterPreview.Animation.Stand, text: qsTr("Stand") },
                { value: CharacterPreview.Animation.Walk, text: qsTr("Walk") },
                { value: CharacterPreview.Animation.Mine, text: qsTr("Mine") }
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

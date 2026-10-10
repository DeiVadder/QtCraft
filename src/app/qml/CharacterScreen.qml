pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The player's look: outfit, hair style and four colours. Changes apply at once (also in a running
// game) and are stored when the screen closes.
Popup {
    id: screen

    required property GameController game

    // Colours offered for each part; the settings file may hold any other colour.
    readonly property var skinColors: ["#f3d2b3", "#e8b48f", "#d8a07a", "#b97b56", "#8d5a3b", "#5c3a24"]  // not a UI colour: character colour choices
    readonly property var hairColors: ["#f1d77a", "#c99a4e", "#8a5a2b", "#4a3020", "#1e1a18", "#b23a24", "#9a9a9a"]  // not a UI colour: character colour choices
    readonly property var clothColors: ["#3a7bc8", "#2f8f4e", "#c83a3a", "#d8a030", "#7a4ab8", "#e8e8e8", "#34406a", "#3a3a3a",  // not a UI colour: character colour choices
        "#8a5a2b"]  // not a UI colour: character colour choices

    function setPart(key, value) {
        const look = game.settings.character.look;
        look[key] = value;
        game.settings.character.look = look;
    }

    // Selects the stored outfit and hair; the lists may have changed (language, mods) since.
    function showStoredLook() {
        outfitBox.currentIndex = outfitBox.indexOfValue(game.settings.character.look.outfit);
        hairBox.currentIndex = hairBox.indexOfValue(game.settings.character.look.hair);
    }

    // On the left, so the character stays visible in the front view.
    x: 32
    y: (parent.height - height) / 2
    width: Math.min(parent.width - 64, 560)
    modal: true
    focus: true
    padding: 20

    onAboutToShow: showStoredLook()
    onClosed: game.settings.saveSettings()

    Connections {
        target: screen.game.settings.character

        function onAppearancesChanged() {
            screen.showStoredLook();
        }
    }

    component ColorRow: RowLayout {
        id: row

        required property string label
        required property string key
        required property var colors

        Layout.fillWidth: true
        spacing: 6

        Label {
            Layout.preferredWidth: 110
            text: row.label
        }
        Repeater {
            model: row.colors

            delegate: Rectangle {
                id: swatch

                required property string modelData

                width: 28
                height: 28
                radius: 4
                color: modelData
                border.width: screen.game.settings.character.look[row.key] === modelData ? 3 : 1
                border.color: screen.game.settings.character.look[row.key] === modelData ? Theme.text : Theme.slotBorder

                MouseArea {
                    anchors.fill: parent
                    onClicked: screen.setPart(row.key, swatch.modelData)
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        focus: true
        // Keys the controls leave end here; Esc still closes the screen, Tab moves the focus.
        Keys.onPressed: event => event.accepted = event.key !== Qt.Key_Escape && event.key !== Qt.Key_Tab
                                                  && event.key !== Qt.Key_Backtab
        spacing: 10

        Label {
            text: qsTr("Character")
            font.pixelSize: 24
            font.bold: true
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 12

            Label {
                Layout.preferredWidth: 110
                text: qsTr("Outfit")
            }
            ComboBox {
                id: outfitBox

                Layout.fillWidth: true
                textRole: "label"
                valueRole: "name"
                model: screen.game.settings.character.outfits
                onActivated: screen.setPart("outfit", currentValue)
            }

            Label {
                text: qsTr("Hair")
            }
            ComboBox {
                id: hairBox

                Layout.fillWidth: true
                textRole: "label"
                valueRole: "name"
                model: screen.game.settings.character.hairStyles
                onActivated: screen.setPart("hair", currentValue)
            }
        }

        ColorRow {
            label: qsTr("Skin")
            key: "skin"
            colors: screen.skinColors
        }
        ColorRow {
            label: qsTr("Hair colour")
            key: "hairColor"
            colors: screen.hairColors
        }
        ColorRow {
            label: qsTr("Shirt")
            key: "shirt"
            colors: screen.clothColors
        }
        ColorRow {
            label: qsTr("Trousers")
            key: "pants"
            colors: screen.clothColors
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Press %1 in the game to look at your character from behind or from the front.").arg(screen.game.settings.keys.labels.cameraView)
            wrapMode: Text.Wrap
            opacity: 0.7
        }

        Button {
            Layout.alignment: Qt.AlignRight
            text: qsTr("Done")
            highlighted: true
            onClicked: screen.close()
        }
    }
}

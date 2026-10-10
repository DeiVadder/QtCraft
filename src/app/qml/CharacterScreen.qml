pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The player's look: outfit, hair style, cape, four colours and presets, next to a live preview of the
// character as other players see it (decision E-231). Changes apply at once (also in a running
// game) and are stored when the screen closes.
Popup {
    id: screen

    required property GameController game
    readonly property var character: game.settings.character

    function setPart(key, value) {
        const look = character.look;
        look[key] = value;
        character.look = look;
    }

    // Selects the stored outfit and hair; the lists may have changed (language, mods) since.
    function showStoredLook() {
        outfitBox.currentIndex = outfitBox.indexOfValue(character.look.outfit);
        hairBox.currentIndex = hairBox.indexOfValue(character.look.hair);
        capeBox.currentIndex = capeBox.indexOfValue(character.look.cape);
    }

    x: 32
    y: (parent.height - height) / 2
    width: Math.min(parent.width - 64, 940)
    modal: true
    focus: true
    padding: 20

    onAboutToShow: showStoredLook()
    onClosed: game.settings.saveSettings()

    Connections {
        target: screen.character

        function onAppearancesChanged() {
            screen.showStoredLook();
        }
        function onLookChanged() {
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
            id: swatches

            model: row.colors

            // Tab reaches every swatch; arrows move along the row, Space or Enter chooses.
            delegate: Rectangle {
                id: swatch

                required property string modelData
                required property int index
                readonly property bool chosen: screen.character.look[row.key] === modelData

                function moveFocus(step) {
                    const next = swatches.itemAt(index + step);
                    if (next)
                        next.forceActiveFocus(Qt.TabFocusReason);
                }

                width: 28
                height: 28
                radius: 4
                color: modelData
                border.width: chosen ? 3 : 2
                border.color: chosen ? Theme.heading : Theme.swatchBorder
                activeFocusOnTab: true
                Accessible.role: Accessible.RadioButton
                Accessible.name: row.label + " " + modelData
                Accessible.checked: chosen
                Keys.onLeftPressed: moveFocus(-1)
                Keys.onRightPressed: moveFocus(1)
                Keys.onSpacePressed: screen.setPart(row.key, modelData)
                Keys.onReturnPressed: screen.setPart(row.key, modelData)
                Keys.onEnterPressed: screen.setPart(row.key, modelData)

                // Focus ring outside the edge.
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -4
                    radius: 6
                    color: Theme.clear
                    border.width: 2
                    border.color: Theme.link
                    visible: swatch.activeFocus
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        screen.setPart(row.key, swatch.modelData);
                        swatch.forceActiveFocus(Qt.MouseFocusReason);
                    }
                }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 20

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
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
                    model: screen.character.outfits
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
                    model: screen.character.hairStyles
                    onActivated: screen.setPart("hair", currentValue)
                }

                Label {
                    text: qsTr("Cape")
                }
                ComboBox {
                    id: capeBox

                    Layout.fillWidth: true
                    textRole: "label"
                    valueRole: "name"
                    model: screen.character.capes
                    onActivated: screen.setPart("cape", currentValue)
                }
            }

            ColorRow {
                label: qsTr("Skin")
                key: "skin"
                colors: screen.character.skinColors
            }
            ColorRow {
                label: qsTr("Hair colour")
                key: "hairColor"
                colors: screen.character.hairColors
            }
            ColorRow {
                label: qsTr("Shirt")
                key: "shirt"
                colors: screen.character.clothColors
            }
            ColorRow {
                label: qsTr("Trousers")
                key: "pants"
                colors: screen.character.clothColors
            }

            CharacterPresets {
                Layout.fillWidth: true
                character: screen.character
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Press %1 in the game to look at your character from behind or from the front.").arg(screen.game.settings.keys.labels.cameraView)
                wrapMode: Text.Wrap
                opacity: 0.7
            }

            RowLayout {
                Layout.fillWidth: true

                Button {
                    text: qsTr("Random")
                    onClicked: screen.character.randomize()
                }
                Button {
                    text: qsTr("Reset")
                    onClicked: screen.character.reset()
                }
                Item {
                    Layout.fillWidth: true
                }
                Button {
                    text: qsTr("Done")
                    highlighted: true
                    onClicked: screen.close()
                }
            }
        }

        CharacterPreviewPanel {
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            Layout.minimumHeight: 460
            game: screen.game
        }
    }
}

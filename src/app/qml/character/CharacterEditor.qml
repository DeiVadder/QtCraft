// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The player's look: outfit, hair style, cape, four colours and presets, next to a live preview of the
// character as other players see it (decision E-231). It edits a draft (decision E-852): the preview
// shows it at once, "Save" applies it (also in a running game; the settings store it), "Discard"
// goes back to the saved look; leaving with unsaved changes asks first (leave()). Presets change the
// draft only. The character screen (in the game) and the main menu's character page show it; they
// open the draft with begin() and close it with end().
RowLayout {
    id: screen

    required property GameController game
    readonly property var character: game.settings.character
    // "Close" in the head (the screen over the game, rule 1 of Menue-Pruefung E.0); the main menu's
    // page has the side bar.
    property bool showClose: false

    signal closeRequested

    function begin() {
        character.beginDraft();
        showStoredLook();
    }

    function end() {
        leaveDialog.close();
        character.endDraft();
    }

    function save() {
        character.saveDraft();
    }

    // Runs `proceed` at once without unsaved changes, else after the player saved or discarded
    // them; keeping on editing runs nothing.
    function leave(proceed: var) {
        if (character.dirty)
            leaveDialog.ask(proceed);
        else
            proceed();
    }

    // Runs `action` from a footer button that disables itself with it (Save, Discard): the focus
    // moves on to "Random" first, so it stays in the editor and Esc and B still work.
    function keepFocus(button: Item, action: var) {
        const hadFocus = button.activeFocus;
        action();
        if (hadFocus)
            randomButton.forceActiveFocus(Qt.TabFocusReason);
    }

    // Selects the stored outfit and hair; the lists may have changed (language, mods) since.
    function showStoredLook() {
        outfitBox.currentIndex = outfitBox.indexOfValue(character.look.outfit);
        hairBox.currentIndex = hairBox.indexOfValue(character.look.hair);
        capeBox.currentIndex = capeBox.indexOfValue(character.look.cape);
    }

    spacing: Ui.space.xl

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
        spacing: Ui.space.s

        Label {
            Layout.preferredWidth: Ui.size.label
            text: row.label
            font.pixelSize: Ui.type.body
            color: Theme.text
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

                width: Ui.size.swatch
                height: Ui.size.swatch
                radius: Ui.radius
                color: modelData
                border.width: chosen ? Ui.size.bar : Ui.size.bevelDeep
                border.color: chosen ? Theme.heading : Theme.swatchBorder
                activeFocusOnTab: true
                Accessible.role: Accessible.RadioButton
                Accessible.name: row.label + " " + modelData
                Accessible.checked: chosen
                Keys.onLeftPressed: moveFocus(-1)
                Keys.onRightPressed: moveFocus(1)
                Keys.onSpacePressed: screen.character.setPart(row.key, modelData)
                Keys.onReturnPressed: screen.character.setPart(row.key, modelData)
                Keys.onEnterPressed: screen.character.setPart(row.key, modelData)

                // The gold outline outside the edge.
                FocusOutline {
                    visible: swatch.activeFocus
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        screen.character.setPart(row.key, swatch.modelData);
                        swatch.forceActiveFocus(Qt.MouseFocusReason);
                    }
                }
            }
        }
    }

    // A group's heading (CH-1 of docs/Menue-Pruefung.md).
    component GroupHeading: Label {
        Layout.topMargin: Ui.space.s
        font.pixelSize: Ui.type.group
        font.bold: true
        color: Theme.heading
        Accessible.role: Accessible.Heading
        Accessible.name: text
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        focus: true
        // Keys the controls leave end here; Esc still closes the screen, Tab moves the focus.
        Keys.onPressed: event => event.accepted = event.key !== Qt.Key_Escape && event.key !== Qt.Key_Tab
                                                  && event.key !== Qt.Key_Backtab
        spacing: Ui.space.m

        Label {
            visible: !screen.showClose
            text: qsTr("Character")
            font.pixelSize: Ui.type.title
            font.bold: true
            color: Theme.text
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }
        ScreenHeader {
            Layout.fillWidth: true
            visible: screen.showClose
            title: qsTr("Character")
            closeName: "characterClose"
            padKey: screen.game.gamepad.active ? screen.game.gamepad.pads.texts.east : ""
            onCloseRequested: screen.leave(() => screen.closeRequested())
        }

        GroupHeading {
            text: qsTr("Appearance")
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Ui.space.m
            rowSpacing: Ui.space.s

            Label {
                Layout.preferredWidth: Ui.size.label
                text: qsTr("Outfit")
                font.pixelSize: Ui.type.body
                color: Theme.text
            }
            ThemedComboBox {
                id: outfitBox

                Accessible.name: qsTr("Outfit")
                Layout.fillWidth: true
                textRole: "label"
                valueRole: "name"
                model: screen.character.outfits
                onActivated: screen.character.setPart("outfit", currentValue)
            }

            Label {
                text: qsTr("Hair")
                font.pixelSize: Ui.type.body
                color: Theme.text
            }
            ThemedComboBox {
                id: hairBox

                Accessible.name: qsTr("Hair")
                Layout.fillWidth: true
                textRole: "label"
                valueRole: "name"
                model: screen.character.hairStyles
                onActivated: screen.character.setPart("hair", currentValue)
            }

            Label {
                text: qsTr("Cape")
                font.pixelSize: Ui.type.body
                color: Theme.text
            }
            ThemedComboBox {
                id: capeBox

                Accessible.name: qsTr("Cape")
                Layout.fillWidth: true
                textRole: "label"
                valueRole: "name"
                model: screen.character.capes
                onActivated: screen.character.setPart("cape", currentValue)
            }
        }

        GroupHeading {
            text: qsTr("Colours")
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

        GroupHeading {
            text: qsTr("Presets")
        }
        CharacterPresets {
            Layout.fillWidth: true
            character: screen.character
        }

        // Keeps the foot at the bottom (CH-2 of docs/Menue-Pruefung.md).
        Item {
            Layout.fillHeight: true
        }

        // The foot: the look's shortcuts on the left, the draft's Discard and Save on the right
        // (decision E-852); the note says that something is unsaved.
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Ui.space.s
            spacing: Ui.space.s

            GameButton {
                id: randomButton

                objectName: "characterRandom"
                large: true
                text: qsTr("Random")
                onClicked: screen.character.randomize()
            }
            GameButton {
                large: true
                text: qsTr("Default look")
                onClicked: screen.character.reset()
            }
            Label {
                objectName: "unsavedNote"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                text: screen.character.dirty ? qsTr("Unsaved changes") : ""
                font.pixelSize: Ui.type.body
                font.bold: true
                color: Theme.heading
                elide: Text.ElideRight
            }
            GameButton {
                id: discardButton

                objectName: "characterDiscard"
                large: true
                enabled: screen.character.dirty
                text: qsTr("Discard")
                onClicked: screen.keepFocus(discardButton, () => screen.character.discardDraft())
            }
            GameButton {
                id: saveButton

                objectName: "characterSave"
                kind: GameButton.Primary
                large: true
                enabled: screen.character.dirty
                text: qsTr("Save")
                onClicked: screen.keepFocus(saveButton, () => screen.save())
            }
        }
    }

    CharacterPreviewPanel {
        Layout.preferredWidth: Ui.size.preview
        Layout.fillHeight: true
        Layout.minimumHeight: Ui.size.previewMinimum
        game: screen.game
    }

    UnsavedChangesDialog {
        id: leaveDialog

        objectName: "characterLeaveDialog"
        onSaveChosen: screen.save()
        onDiscardChosen: screen.character.discardDraft()
    }
}

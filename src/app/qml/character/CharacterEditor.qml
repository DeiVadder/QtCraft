pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The player's look: outfit, hair style, cape, four colours and presets, next to a live preview of the
// character as other players see it (decision E-231). It edits a draft (decision E-852): the preview
// shows it at once, "Save" applies it (also in a running game) and stores the settings, "Discard"
// goes back to the saved look; leaving with unsaved changes asks first (leave()). Presets change the
// draft only. The character screen (in the game) and the main menu's character page show it; they
// open the draft with begin() and close it with end().
RowLayout {
    id: screen

    required property GameController game
    readonly property var character: game.settings.character
    // "Close" in the footer (the screen over the game); the main menu's page has the side bar.
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
        game.settings.saveSettings();
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
                Keys.onSpacePressed: screen.setPart(row.key, modelData)
                Keys.onReturnPressed: screen.setPart(row.key, modelData)
                Keys.onEnterPressed: screen.setPart(row.key, modelData)

                // The gold outline outside the edge.
                FocusOutline {
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

    ColumnLayout {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        focus: true
        // Keys the controls leave end here; Esc still closes the screen, Tab moves the focus.
        Keys.onPressed: event => event.accepted = event.key !== Qt.Key_Escape && event.key !== Qt.Key_Tab
                                                  && event.key !== Qt.Key_Backtab
        spacing: Ui.space.m

        Label {
            text: qsTr("Character")
            font.pixelSize: Ui.type.title
            font.bold: true
            color: Theme.text
            Accessible.role: Accessible.Heading
            Accessible.name: text
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
                onActivated: screen.setPart("outfit", currentValue)
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
                onActivated: screen.setPart("hair", currentValue)
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
            font.pixelSize: Ui.type.help
            color: Theme.textMuted
        }

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
        }

        // Save or discard the draft (decision E-852); the note says that something is unsaved.
        RowLayout {
            Layout.fillWidth: true
            spacing: Ui.space.s

            Label {
                objectName: "unsavedNote"
                Layout.fillWidth: true
                text: screen.character.dirty ? qsTr("Unsaved changes") : ""
                font.pixelSize: Ui.type.body
                font.bold: true
                color: Theme.heading
                elide: Text.ElideRight
            }
            GameButton {
                visible: screen.showClose
                kind: GameButton.Quiet
                large: true
                text: qsTr("Close")
                onClicked: screen.leave(() => screen.closeRequested())
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

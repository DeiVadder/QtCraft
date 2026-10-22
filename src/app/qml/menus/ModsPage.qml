pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The main menu's page "Mods & packs" (decision E-852): the mods found in the mods directory with
// their state; switches enable or disable a mod for the next world start. Texture packs, which apply
// at once, are in the settings: the footer leads there.
ColumnLayout {
    id: screen

    required property GameController game

    // The page should be shown (open(), also the test hook "@<ms> open:modsScreen").
    signal openRequested
    // "Texture packs" was chosen: the menu shows them in the settings.
    signal texturePacksRequested

    function open() {
        game.library.refreshMods();
        openRequested();
    }

    // Back from the texture packs in the settings, "Texture packs …" has the focus again.
    function focusTexturePacks() {
        texturePacksLink.forceActiveFocus(Qt.TabFocusReason);
    }

    spacing: 0

    ColumnLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        // Keys the controls leave end here; Esc still leaves the page, Tab moves the focus.
        Keys.onPressed: event => event.accepted = event.key !== Qt.Key_Escape && event.key !== Qt.Key_Tab
                                                  && event.key !== Qt.Key_Backtab
        spacing: Ui.space.m

        Label {
            text: qsTr("Mods")
            font.pixelSize: Ui.type.title
            font.bold: true
            color: Theme.text
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }

        Label {
            Layout.fillWidth: true
            // The list's scroll bar gutter, so the right edges line up with its rows.
            Layout.rightMargin: Ui.space.l
            text: screen.game.library.modsDirectory.length > 0
                  ? qsTr("Changes take effect when a world is started. Mods are loaded from this folder:")
                  : qsTr("Mods are switched off for this session.")
            wrapMode: Text.Wrap
            font.pixelSize: Ui.type.body
            color: Theme.textSecondary
        }

        // The folder as text that can be selected and breaks anywhere (docs/UI-Leitfaden.md 5).
        TextEdit {
            Layout.fillWidth: true
            Layout.rightMargin: Ui.space.l
            visible: screen.game.library.modsDirectory.length > 0
            readOnly: true
            selectByMouse: true
            textFormat: TextEdit.PlainText
            text: screen.game.library.modsDirectory
            wrapMode: TextEdit.WrapAnywhere
            font.pixelSize: Ui.type.help
            color: Theme.textMuted
            selectionColor: Theme.accent
            selectedTextColor: Theme.accentText
            Accessible.role: Accessible.StaticText
            Accessible.name: qsTr("Mods folder: %1").arg(text)
        }

        Hint {
            Layout.fillWidth: true
            Layout.rightMargin: Ui.space.l
            kind: Hint.Warning
            text: qsTr("Opening a world without a mod it was played with removes that mod's blocks and items from it.")
        }

        ListView {
            id: modList

            Accessible.name: qsTr("Mods")
            Accessible.role: Accessible.List
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Ui.space.s
            boundsBehavior: Flickable.StopAtBounds
            model: screen.game.library.mods
            ScrollBar.vertical: ThemedScrollBar {}

            Label {
                anchors.centerIn: parent
                visible: modList.count === 0
                text: qsTr("No mods installed. Put a mod folder with a pack.json into the mods directory.")
                wrapMode: Text.Wrap
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Ui.type.body
                color: Theme.textSecondary
            }

            delegate: ListRow {
                id: modRow

                required property var modelData

                // A gutter on the right for the scroll bar.
                width: ListView.view.width - Ui.space.l
                title: modelData.version.length > 0 ? qsTr("%1 %2").arg(modelData.name).arg(modelData.version) : modelData.name
                // Written by the mod's author: never rich text (E-272, ListRow shows plain text).
                subtitle: modelData.description

                Label {
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                    text: modRow.modelData.state === "loaded" ? qsTr("Active")
                        : modRow.modelData.state === "disabled" ? qsTr("Disabled")
                        : qsTr("Not loaded: %1").arg(modRow.modelData.reason)
                    font.pixelSize: Ui.type.help
                    color: modRow.modelData.state === "failed" ? Theme.error : Theme.textMuted
                    wrapMode: Text.Wrap
                }

                actions: GameSwitch {
                    Accessible.name: qsTr("Enable %1").arg(modRow.modelData.name)
                    visible: modRow.modelData.id.length > 0
                    checked: modRow.modelData.state !== "disabled"
                    onToggled: screen.game.library.setModEnabled(modRow.modelData.id, checked)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.rightMargin: Ui.space.l
            spacing: Ui.space.s

            GameButton {
                large: true
                text: qsTr("Open mods folder")
                enabled: screen.game.library.modsDirectory.length > 0
                onClicked: screen.game.library.openModsFolder()
            }
            GameButton {
                id: texturePacksLink

                objectName: "texturePacksLink"
                large: true
                text: qsTr("Texture packs…")
                onClicked: screen.texturePacksRequested()
            }
            Item {
                Layout.fillWidth: true
            }
        }
    }
}

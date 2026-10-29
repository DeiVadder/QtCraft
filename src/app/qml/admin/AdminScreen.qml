pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtCraft

// "Manage server" (decision E-863, stage 77.2): an operator's view of the server over the admin
// channel, one level down from the pause menu (SidebarScreen, docs/UI-Leitfaden.md 8b). The side
// bar has the back entry and the sections overview, players, chat, commands, lists, game rules, log
// and server; the chosen one shows on the right. The screen follows the server's status, chat and
// log only while it is open. Answers and refusals show as a notice above the section. Kicking,
// banning and stopping ask first in a modal dialog; leaving the game rules with unsaved changes asks
// whether to save them. The back entry, Esc and the pad's B close it; the pad's shoulders and
// Ctrl+Tab switch the section.
SidebarScreen {
    id: screen

    enum Section {
        Overview,
        Players,
        Chat,
        Commands,
        Lists,
        Settings,
        Log,
        Server
    }

    required property GameController game
    readonly property AdminController admin: game.admin
    // The section shown; the side bar's choice waits for the unsaved-changes question.
    property int section: AdminScreen.Overview

    // The screen was closed (back entry, Esc, the pad's B or close()).
    signal closed

    function open() {
        section = AdminScreen.Overview;
        nav.currentIndex = section;
        visible = true;
        admin.open();
        forceActiveFocus();
        focusPage();
    }

    // Closes at once; leave() asks first about unsaved game rules.
    // An unsaved draft stays with the controller and shows again on the next open.
    function close() {
        if (!visible)
            return;
        visible = false;
        admin.close();
        closed();
    }

    function leave() {
        if (screen.admin.dirty)
            unsavedDialog.ask(() => screen.close());
        else
            close();
    }

    // The pad's shoulders switch the section, its B leaves the screen.
    function gamepadCommand(command: string): bool {
        if (command === "back") {
            leave();
            return true;
        }
        return nav.takeCommand(command);
    }

    anchors.fill: parent
    visible: false
    inGame: true
    title: qsTr("Manage server")
    backText: qsTr("‹ Pause menu")
    bottomRoom: game.gamepad.focusShown ? Ui.size.padHints : 0
    shownPage: pages.children[pages.currentIndex] ?? null
    onBackRequested: leave()
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape)
            screen.leave();
        event.accepted = event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab;
    }

    entries: [
        NavList {
            id: nav

            objectName: "adminNav"
            Layout.fillWidth: true
            model: [qsTr("Overview"), qsTr("Players"), qsTr("Chat"), qsTr("Commands"), qsTr("Lists"), qsTr("Game rules"),
                qsTr("Log"), qsTr("Server")]
            notes: ["", String(screen.admin.players.length), "", "", "", screen.admin.dirty ? qsTr("unsaved") : "", "", ""]
            // The focus goes onto the new section, unless leaving the game rules asks first.
            onSwitched: {
                if (!unsavedDialog.visible)
                    screen.focusPage();
            }
            // Leaving the game rules with unsaved changes asks first; the side bar keeps them until then.
            onCurrentIndexChanged: {
                if (currentIndex === screen.section)
                    return;
                const target = currentIndex;
                if (screen.section === AdminScreen.Settings && screen.admin.dirty) {
                    currentIndex = screen.section;
                    unsavedDialog.ask(() => {
                        screen.section = target;
                        nav.currentIndex = target;
                        screen.focusPage();
                    });
                } else {
                    screen.section = target;
                }
            }
        },
        Item {
            Layout.fillHeight: true
        }
    ]

    ColumnLayout {
        anchors.fill: parent
        spacing: Ui.space.m

        RowLayout {
            Layout.fillWidth: true
            visible: screen.admin.notice.length > 0
            spacing: Ui.space.s

            Hint {
                objectName: "adminNotice"
                Layout.fillWidth: true
                kind: screen.admin.noticeKind === AdminController.Notice.Refused ? Hint.Error : Hint.Info
                text: screen.admin.notice
            }
            GameButton {
                kind: GameButton.Quiet
                text: qsTr("Hide")
                Accessible.name: qsTr("Hide the notice")
                onClicked: screen.admin.clearNotice()
            }
        }

        StackLayout {
            id: pages

            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: screen.section

            AdminOverviewPage {
                admin: screen.admin
            }
            AdminPlayersPage {
                admin: screen.admin
                dialog: actionDialog
            }
            AdminChatPage {
                admin: screen.admin
            }
            AdminCommandsPage {
                admin: screen.admin
            }
            AdminListsPage {
                admin: screen.admin
                dialog: actionDialog
            }
            AdminSettingsPage {
                id: settingsPage

                admin: screen.admin
            }
            AdminLogPage {
                admin: screen.admin
            }
            AdminServerPage {
                admin: screen.admin
                dialog: actionDialog
                remote: screen.game.multiplayer.networkGame
                persistent: screen.game.persistent
            }
        }
    }

    AdminActionDialog {
        id: actionDialog

        reasonLength: screen.admin.reasonLength
    }

    // The charts are built only while the overview shows them.
    Binding {
        target: screen.admin
        property: "chartsShown"
        value: screen.visible && screen.section === AdminScreen.Overview
    }

    UnsavedChangesDialog {
        id: unsavedDialog

        onSaveChosen: screen.admin.saveSettings()
        onDiscardChosen: screen.admin.discardSettings()
    }
}

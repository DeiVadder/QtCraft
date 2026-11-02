// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The handbook, one level down the side bar (SidebarScreen, docs/UI-Leitfaden.md 8b, decision
// E-852), in the main menu and in the game alike: the bar shows the back entry, the search and the
// categories; on the right the entries of the chosen category (or the search results) and the open
// page. Links and recipe cells open other pages; "Previous page" and Backspace return. The back
// entry, Esc, the handbook key and the pad's B close it and give the keyboard back to the screen
// below; the pad's shoulders switch the category. Page, history, category, search and the entry list
// are the controller's (HandbookNavigation, decision E-954); this screen keeps focus and scrolling.
SidebarScreen {
    id: screen

    required property GameController game

    readonly property HandbookNavigation browse: game.handbook.navigation
    property Item focusBefore: null
    // The item that had the focus outside the handbook until it opened (Main.qml keeps it).
    property Item focusOutside: null

    readonly property var categories: game.handbook.model.categories

    function close() {
        game.handbook.close();
    }

    // The first entry of the shown category takes the focus (after LB/RB, docs/UI-Leitfaden.md 10);
    // the list makes its rows for the new category first.
    function focusFirstEntry() {
        entryList.forceLayout();
        entryList.positionViewAtBeginning();
        entryList.itemAtIndex(browse.firstEntry)?.forceActiveFocus(Qt.TabFocusReason);
    }

    // Marks the shown category in the bar; none while search results show.
    function markCategory() {
        nav.currentIndex = browse.categoryIndex;
    }

    // Gives the focus back to the item that had it before the handbook opened, once the screens below
    // show again (they hide while the handbook covers them).
    function returnFocus() {
        const before = focusBefore;
        focusBefore = null;
        if (before)
            Qt.callLater(() => before?.forceActiveFocus(Qt.TabFocusReason));
    }

    // A page was shown: from its top, its row marked in the list.
    function showPage() {
        pageView.contentY = 0;
        entryList.select();
    }

    // The pad's B closes the handbook, its shoulders switch the category (QcMenuNavigator asks the
    // items above the focus).
    function gamepadCommand(command: int): bool {
        if (command === GamepadMenuInput.Command.Back) {
            close();
            return true;
        }
        return nav.takeCommand(command);
    }

    anchors.fill: parent
    visible: game.handbook.isOpen
    focus: visible
    inGame: game.phase !== GameController.Phase.Menu
    title: qsTr("Handbook")
    bottomRoom: game.gamepad.focusShown ? Ui.size.padHints : 0
    // The back entry names where it leads.
    backText: !inGame ? qsTr("‹ Main menu")
              : game.overlay === GameController.Overlay.Pause ? qsTr("‹ Pause menu")
              : game.overlay === GameController.Overlay.None ? qsTr("‹ Back to game") : qsTr("‹ Back")
    onBackRequested: close()
    Component.onCompleted: markCategory()

    // The controller chose the start page when the handbook opened (also while this screen was not
    // made yet); here only the focus and the scrolling follow.
    onVisibleChanged: {
        if (visible) {
            focusBefore = focusOutside ?? Window.activeFocusItem;
            showPage();
            markCategory();
            if (game.handbook.query.length > 0)
                searchField.forceActiveFocus();
            else
                forceActiveFocus();
        } else {
            returnFocus();
        }
    }

    Keys.onPressed: event => {
        if (screen.game.settings.keys.screenKey(event.key, event.isAutoRepeat, KeyControls.ClosingKey.HandbookKey) === KeyControls.ScreenKey.Close) {
            screen.close();
        } else if (event.key === Qt.Key_Backspace || (event.key === Qt.Key_Left && (event.modifiers & Qt.AltModifier))) {
            screen.browse.back();
        }
        // Other keys end here as well; Tab moves the focus.
        event.accepted = event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab;
    }

    Connections {
        target: screen.browse

        function onListChanged() {
            screen.markCategory();
        }

        function onPageShown() {
            screen.showPage();
        }
    }

    entries: [
        GameTextField {
            id: searchField

            Layout.fillWidth: true
            Layout.leftMargin: Ui.space.l
            Layout.rightMargin: Ui.space.s
            Accessible.name: qsTr("Search the handbook")
            placeholderText: qsTr("Search …")
            // The controller keeps the search; typing changes it, opening and the side bar set it.
            text: screen.browse.search
            onTextEdited: screen.browse.search = text
            Keys.onPressed: event => {
                // A handbook key that types a character (H) goes into the search; one that does not
                // closes the handbook, held or not.
                const typed = event.text.length > 0 && event.text >= " ";
                if (!typed && screen.game.settings.keys.screenKey(event.key, false, KeyControls.ClosingKey.HandbookKey) === KeyControls.ScreenKey.Close) {
                    screen.close();
                    event.accepted = true;
                } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && screen.browse.firstEntry >= 0) {
                    screen.browse.openRow(screen.browse.firstEntry);
                    event.accepted = true;
                } else if (event.key === Qt.Key_Down) {
                    entryList.forceActiveFocus(Qt.TabFocusReason);
                    event.accepted = true;
                }
            }
        },
        // The categories scroll when the window is too low for all of them.
        Flickable {
            id: categoryView

            // Scrolls the category `entry` into view.
            function reveal(entry: Item) {
                const top = entry.mapToItem(nav, 0, 0).y;
                if (top < contentY)
                    contentY = top;
                else if (top + entry.height > contentY + height)
                    contentY = top + entry.height - height;
            }

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: nav.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ThemedScrollBar {}

            NavList {
                id: nav

                objectName: "handbookNav"
                width: categoryView.width
                currentIndex: -1
                model: screen.categories.map(entry => entry.title)
                notes: screen.categories.map(entry => String(entry.count))
                onEntryFocused: entry => categoryView.reveal(entry)
                // The shoulders and Ctrl+Tab put the focus on the category's first entry.
                onSwitched: screen.focusFirstEntry()
                // A category chosen in the bar (click, Ctrl+Tab, shoulders) ends a search.
                onCurrentIndexChanged: screen.browse.chooseCategory(currentIndex)
            }
        }
    ]

    ColumnLayout {
        anchors.fill: parent
        spacing: Ui.space.m

        RowLayout {
            Layout.fillWidth: true
            spacing: Ui.space.l

            GameButton {
                kind: GameButton.Quiet
                text: qsTr("‹ Previous page")
                enabled: screen.browse.canGoBack
                onClicked: screen.browse.back()
            }
            // The keys as plates (HB-4 of docs/Menue-Pruefung.md); the pad's buttons stand at the
            // window's bottom edge while it leads (PadHints).
            KeyHintRow {
                objectName: "handbookKeyHints"
                Layout.fillWidth: true
                visible: !screen.game.gamepad.active
                entries: [
                    { keys: [qsTr("Click")], text: qsTr("open a name or an item") },
                    { keys: [qsTr("Backspace")], text: qsTr("previous page") },
                    { keys: [qsTr("Esc"), screen.game.settings.keys.labels.handbook], text: qsTr("close") }
                ]
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Ui.space.m

            ListView {
                id: entryList

                Accessible.name: qsTr("Entries")
                // Highlights the open page in the list and scrolls it into view.
                function select() {
                    const index = screen.browse.currentRow;
                    currentIndex = index;
                    if (index >= 0)
                        positionViewAtIndex(index, ListView.Contain);
                }

                Layout.preferredWidth: Ui.size.handbookEntries
                Layout.fillHeight: true
                clip: true
                spacing: Ui.size.line
                model: screen.browse.listed
                currentIndex: -1
                onModelChanged: Qt.callLater(select)
                keyNavigationEnabled: true
                Keys.onReturnPressed: {
                    if (currentIndex >= 0)
                        screen.browse.openRow(currentIndex);
                }
                ScrollBar.vertical: ThemedScrollBar {}

                Label {
                    anchors.centerIn: parent
                    visible: screen.browse.searching && entryList.count === 0
                    text: qsTr("Nothing found.")
                    color: Theme.textMuted
                }

                delegate: HandbookEntryRow {
                    width: ListView.view.width
                    searching: screen.browse.searching
                    open: screen.browse.current
                    onClicked: screen.browse.navigate(modelData.id, true)
                }
            }

            Rectangle {
                Layout.preferredWidth: Ui.size.line
                Layout.fillHeight: true
                color: Theme.separator
            }

            HandbookPage {
                id: pageView

                Layout.fillWidth: true
                Layout.fillHeight: true
                page: screen.browse.page
                onLinkActivated: target => screen.browse.navigate(target, true)
            }
        }
    }
}

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
// below; the pad's shoulders switch the category.
SidebarScreen {
    id: screen

    required property GameController game

    property string current
    property var page: ({})
    property var history: []
    property string category
    property Item focusBefore: null
    // The item that had the focus outside the handbook until it opened (Main.qml keeps it).
    property Item focusOutside: null

    readonly property bool searching: searchField.text.trim().length > 0
    readonly property var categories: game.handbook.model.categories
    // Reads `categories` so the lists follow a rebuilt handbook (other language or mods).
    readonly property var listed: !visible || categories.length === 0 ? []
                                  : (searching ? game.handbook.model.search(searchField.text) : game.handbook.model.entries(category))

    function navigate(target, remember) {
        const id = game.handbook.model.resolve(target);
        if (id.length === 0 || id === current)
            return;
        if (remember && current.length > 0)
            history = history.concat([current]);
        current = id;
        game.handbook.page = id;
        page = game.handbook.model.page(id);
        if (!searching)
            category = page.category;
        pageView.contentY = 0;
        entryList.select();
    }

    function back() {
        if (history.length === 0)
            return;
        const previous = history[history.length - 1];
        history = history.slice(0, history.length - 1);
        current = "";
        navigate(previous, false);
    }

    function close() {
        game.handbook.close();
    }

    // The first entry of the shown category takes the focus (after LB/RB, docs/UI-Leitfaden.md 10);
    // the list makes its rows for the new category first.
    function focusFirstEntry() {
        entryList.forceLayout();
        entryList.positionViewAtBeginning();
        const index = listed.findIndex(row => row.id !== undefined);
        entryList.itemAtIndex(index)?.forceActiveFocus(Qt.TabFocusReason);
    }

    // Marks the shown category in the bar; none while search results show.
    function markCategory() {
        nav.currentIndex = searching ? -1 : categories.findIndex(entry => entry.id === category);
    }

    // The pad's B closes the handbook, its shoulders switch the category (QcMenuNavigator asks the
    // items above the focus).
    function gamepadCommand(command: string): bool {
        if (command === "back") {
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
    onCategoryChanged: markCategory()
    onSearchingChanged: markCategory()
    onCategoriesChanged: markCategory()

    onVisibleChanged: {
        if (visible) {
            focusBefore = focusOutside ?? Window.activeFocusItem;
            history = [];
            searchField.text = game.handbook.query;
            const found = game.handbook.query.length > 0 ? game.handbook.model.search(game.handbook.query)[0] : undefined;
            if (game.handbook.target.length > 0) {
                navigate(game.handbook.target, false);
            } else if (found) {
                navigate(found.id, false);
            } else if (current.length === 0 && game.handbook.page.length === 0) {
                const first = game.handbook.model.entries("playing").find(row => row.id !== undefined);
                if (first)
                    navigate(first.id, false);
            } else {
                // The page where it was left, also after the screen was unloaded (the controller
                // keeps it) or the handbook was rebuilt (language, mods).
                const shown = current.length > 0 ? current : game.handbook.page;
                current = "";
                navigate(shown, false);
            }
            markCategory();
            if (game.handbook.query.length > 0)
                searchField.forceActiveFocus();
            else
                forceActiveFocus();
        } else if (focusBefore) {
            // Once the screens below show again (they hide while the handbook covers them).
            const before = focusBefore;
            focusBefore = null;
            Qt.callLater(() => {
                if (before)
                    before.forceActiveFocus(Qt.TabFocusReason);
            });
        }
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape || (screen.game.settings.keys.matches("handbook", event.key) && !event.isAutoRepeat)) {
            screen.close();
        } else if (event.key === Qt.Key_Backspace || (event.key === Qt.Key_Left && (event.modifiers & Qt.AltModifier))) {
            screen.back();
        }
        // Other keys end here as well; Tab moves the focus.
        event.accepted = event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab;
    }

    entries: [
        GameTextField {
            id: searchField

            Layout.fillWidth: true
            Layout.leftMargin: Ui.space.l
            Layout.rightMargin: Ui.space.s
            Accessible.name: qsTr("Search the handbook")
            placeholderText: qsTr("Search …")
            Keys.onPressed: event => {
                // A handbook key that types a character (H) goes into the search.
                if (event.key === Qt.Key_Escape
                    || (screen.game.settings.keys.matches("handbook", event.key) && (event.text.length === 0 || event.text < " "))) {
                    screen.close();
                    event.accepted = true;
                } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && screen.listed.length > 0) {
                    const first = screen.listed.find(row => row.id !== undefined);
                    if (first)
                        screen.navigate(first.id, true);
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
                onCurrentIndexChanged: {
                    if (currentIndex < 0 || currentIndex >= screen.categories.length)
                        return;
                    screen.category = screen.categories[currentIndex].id;
                    searchField.text = "";
                }
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
                enabled: screen.history.length > 0
                onClicked: screen.back()
            }
            Label {
                Layout.fillWidth: true
                text: screen.game.gamepad.active ? qsTr("%1 on a name or an item opens its page. %2 closes.").arg(screen.game.gamepad.pads.texts.south).arg(screen.game.gamepad.pads.texts.east)
                      : qsTr("Click a name or an item to open its page. Backspace goes back, Esc or %1 closes.").arg(screen.game.settings.keys.labels.handbook)
                font.pixelSize: Ui.type.help
                color: Theme.textMuted
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
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
                    const index = screen.listed.findIndex(row => row.id === screen.current);
                    currentIndex = index;
                    if (index >= 0)
                        positionViewAtIndex(index, ListView.Contain);
                }

                Layout.preferredWidth: Ui.size.handbookEntries
                Layout.fillHeight: true
                clip: true
                spacing: Ui.size.line
                model: screen.listed
                currentIndex: -1
                onModelChanged: Qt.callLater(select)
                keyNavigationEnabled: true
                Keys.onReturnPressed: {
                    const row = screen.listed[currentIndex];
                    if (row && row.id !== undefined)
                        screen.navigate(row.id, true);
                }
                ScrollBar.vertical: ThemedScrollBar {}

                Label {
                    anchors.centerIn: parent
                    visible: screen.searching && entryList.count === 0
                    text: qsTr("Nothing found.")
                    color: Theme.textMuted
                }

                delegate: HandbookEntryRow {
                    width: ListView.view.width
                    searching: screen.searching
                    open: screen.current
                    onClicked: screen.navigate(modelData.id, true)
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
                page: screen.page
                onLinkActivated: target => screen.navigate(target, true)
            }
        }
    }
}

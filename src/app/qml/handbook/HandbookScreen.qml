pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The handbook: categories on the left, the entries of a category (or the search results) in the
// middle and the open page on the right. Links and recipe cells open other pages; Back returns.
// Esc or the handbook key closes it and gives the keyboard back to the screen below.
Rectangle {
    id: screen

    required property GameController game

    property string current
    property var page: ({})
    property var history: []
    property string category
    property Item focusBefore: null

    readonly property bool searching: searchField.text.trim().length > 0
    // Reads `categories` so the lists follow a rebuilt handbook (other language or mods).
    readonly property var listed: !visible || game.handbook.model.categories.length === 0 ? []
                                  : (searching ? game.handbook.model.search(searchField.text) : game.handbook.model.entries(category))

    visible: game.handbook.isOpen
    color: Theme.scrimStrong
    focus: visible

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

    onVisibleChanged: {
        if (visible) {
            focusBefore = Window.activeFocusItem;
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
            if (game.handbook.query.length > 0)
                searchField.forceActiveFocus();
            else
                forceActiveFocus();
        } else if (focusBefore) {
            focusBefore.forceActiveFocus();
            focusBefore = null;
        }
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape || screen.game.settings.keys.matches("handbook", event.key)) {
            screen.close();
        } else if (event.key === Qt.Key_Backspace || (event.key === Qt.Key_Left && (event.modifiers & Qt.AltModifier))) {
            screen.back();
        }
        // Other keys end here as well; Tab moves the focus.
        event.accepted = event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab;
    }

    // Swallows clicks so nothing below reacts; nothing a screen reader offers.
    MouseArea {
        anchors.fill: parent
        Accessible.ignored: true
        acceptedButtons: Qt.AllButtons
        onWheel: wheel => wheel.accepted = true
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: Ui.px(20)
        radius: Ui.px(6)
        color: Theme.panel
        border.color: Theme.panelBorder

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Ui.px(14)
            spacing: Ui.px(10)

            RowLayout {
                Layout.fillWidth: true
                spacing: Ui.px(10)

                Button {
                    text: qsTr("‹ Back")
                    enabled: screen.history.length > 0
                    onClicked: screen.back()
                }

                Label {
                    text: qsTr("Handbook")
                    color: Theme.text
                    font.pixelSize: Ui.px(24)
                    font.bold: true
                }

                Item {
                    Layout.fillWidth: true
                }

                TextField {
                    id: searchField

                    Accessible.name: qsTr("Search the handbook")
                    Layout.preferredWidth: Ui.px(320)
                    placeholderText: qsTr("Search blocks, items, creatures …")
                    selectByMouse: true
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
                            entryList.forceActiveFocus();
                            event.accepted = true;
                        }
                    }
                }

                Button {
                    text: qsTr("Close (Esc)")
                    onClicked: screen.close()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Ui.px(12)

                ListView {
                    id: categoryList

                    Accessible.name: qsTr("Categories")
                    Layout.preferredWidth: Ui.px(190)
                    Layout.fillHeight: true
                    clip: true
                    spacing: 1
                    model: screen.game.handbook.model.categories

                    delegate: ItemDelegate {
                        id: categoryRow

                        required property var modelData

                        width: ListView.view.width
                        highlighted: !screen.searching && modelData.id === screen.category
                        text: modelData.title
                        onClicked: {
                            searchField.text = "";
                            screen.category = modelData.id;
                        }

                        Label {
                            anchors.right: parent.right
                            anchors.rightMargin: Ui.px(10)
                            anchors.verticalCenter: parent.verticalCenter
                            text: categoryRow.modelData.count
                            color: Theme.textMuted
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    color: Theme.separator
                }

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

                    Layout.preferredWidth: Ui.px(270)
                    Layout.fillHeight: true
                    clip: true
                    spacing: 1
                    model: screen.listed
                    currentIndex: -1
                    onModelChanged: Qt.callLater(select)
                    keyNavigationEnabled: true
                    Keys.onReturnPressed: {
                        const row = screen.listed[currentIndex];
                        if (row && row.id !== undefined)
                            screen.navigate(row.id, true);
                    }

                    Label {
                        anchors.centerIn: parent
                        visible: screen.searching && entryList.count === 0
                        text: qsTr("Nothing found.")
                        color: Theme.textMuted
                    }

                    delegate: ItemDelegate {
                        id: entryRow

                        required property var modelData
                        required property int index
                        readonly property bool header: modelData.header !== undefined

                        width: ListView.view.width
                        height: Ui.px(header ? 30 : 36)
                        // The row draws its own content; the name is for screen readers.
                        Accessible.name: header ? modelData.header : modelData.title
                        enabled: !header
                        highlighted: !header && modelData.id === screen.current
                        onClicked: screen.navigate(modelData.id, true)

                        contentItem: RowLayout {
                            spacing: Ui.px(8)

                            Image {
                                Layout.preferredWidth: Ui.px(24)
                                Layout.preferredHeight: Ui.px(24)
                                visible: !entryRow.header
                                source: !entryRow.header && entryRow.modelData.icon.length > 0 ? ItemIcons.source(entryRow.modelData.icon, ItemIcons.generation) : ""
                                sourceSize: Qt.size(24, 24)
                                smooth: false
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0

                                Label {
                                    Layout.fillWidth: true
                                    text: entryRow.header ? entryRow.modelData.header : entryRow.modelData.title
                                    color: entryRow.header ? Theme.heading : Theme.text
                                    font.bold: entryRow.header
                                    elide: Text.ElideRight
                                }

                                Label {
                                    visible: screen.searching && !entryRow.header
                                    text: entryRow.header ? "" : entryRow.modelData.categoryTitle
                                    color: Theme.textMuted
                                    font.pixelSize: Ui.px(11)
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    color: Theme.separator
                }

                Flickable {
                    id: pageView

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: width
                    contentHeight: pageColumn.implicitHeight + Ui.px(20)
                    boundsBehavior: Flickable.StopAtBounds

                    ScrollBar.vertical: ThemedScrollBar {}

                    ColumnLayout {
                        id: pageColumn

                        width: pageView.width - Ui.px(16)
                        spacing: Ui.px(12)

                        RowLayout {
                            spacing: Ui.px(12)

                            Rectangle {
                                Layout.preferredWidth: Ui.px(56)
                                Layout.preferredHeight: Ui.px(56)
                                visible: (screen.page.icon || "").length > 0
                                color: Theme.panelRaised
                                radius: Ui.px(3)

                                Image {
                                    anchors.fill: parent
                                    anchors.margins: Ui.px(6)
                                    source: parent.visible ? ItemIcons.source(screen.page.icon, ItemIcons.generation) : ""
                                    sourceSize: Qt.size(44, 44)
                                    smooth: false
                                }
                            }

                            ColumnLayout {
                                spacing: Ui.px(2)

                                Label {
                                    text: screen.page.title || ""
                                    color: Theme.text
                                    font.pixelSize: Ui.px(26)
                                    font.bold: true
                                }

                                Label {
                                    text: (screen.page.categoryTitle || "") + "  ·  " + (screen.page.packName || "")
                                    color: Theme.textMuted
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: screen.page.text || ""
                            color: Theme.text
                            font.pixelSize: Ui.px(15)
                            wrapMode: Text.Wrap
                            textFormat: Text.MarkdownText
                        }

                        // Facts as a two-column table.
                        Rectangle {
                            Layout.fillWidth: true
                            visible: factRepeater.count > 0
                            implicitHeight: factColumn.implicitHeight + Ui.px(12)
                            color: Theme.rowHighlight
                            radius: Ui.px(4)

                            ColumnLayout {
                                id: factColumn

                                x: Ui.px(8)
                                y: Ui.px(6)
                                width: parent.width - Ui.px(16)
                                spacing: Ui.px(4)

                                Repeater {
                                    id: factRepeater

                                    model: screen.page.facts || []

                                    RowLayout {
                                        id: factRow

                                        required property var modelData

                                        Layout.fillWidth: true
                                        spacing: Ui.px(10)

                                        Label {
                                            Layout.preferredWidth: Ui.px(210)
                                            Layout.alignment: Qt.AlignTop
                                            topPadding: Ui.px(4)
                                            text: factRow.modelData.label
                                            color: Theme.textMuted
                                            wrapMode: Text.Wrap
                                        }

                                        Flow {
                                            Layout.fillWidth: true
                                            spacing: Ui.px(6)

                                            Label {
                                                visible: text.length > 0
                                                topPadding: Ui.px(4)
                                                width: Math.min(implicitWidth, parent.width)
                                                text: factRow.modelData.value
                                                color: Theme.text
                                                wrapMode: Text.Wrap
                                            }

                                            Repeater {
                                                model: factRow.modelData.links

                                                HandbookLink {
                                                    required property var modelData

                                                    link: modelData
                                                    onActivated: target => screen.navigate(target, true)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        Repeater {
                            model: screen.page.sections || []

                            ColumnLayout {
                                id: sectionColumn

                                required property var modelData

                                Layout.fillWidth: true
                                spacing: Ui.px(6)

                                Label {
                                    visible: text.length > 0
                                    text: sectionColumn.modelData.title
                                    color: Theme.heading
                                    font.pixelSize: Ui.px(18)
                                    font.bold: true
                                }

                                Label {
                                    Layout.fillWidth: true
                                    visible: text.length > 0
                                    text: sectionColumn.modelData.text
                                    color: Theme.text
                                    font.pixelSize: Ui.px(15)
                                    wrapMode: Text.Wrap
                                    textFormat: Text.MarkdownText
                                }

                                Flow {
                                    Layout.fillWidth: true
                                    visible: sectionColumn.modelData.recipes.length > 0
                                    spacing: Ui.px(18)

                                    Repeater {
                                        model: sectionColumn.modelData.recipes

                                        HandbookRecipe {
                                            required property var modelData

                                            recipe: modelData
                                            onActivated: target => screen.navigate(target, true)
                                        }
                                    }
                                }

                                Flow {
                                    Layout.fillWidth: true
                                    visible: sectionColumn.modelData.links.length > 0
                                    spacing: Ui.px(6)

                                    Repeater {
                                        model: sectionColumn.modelData.links

                                        HandbookLink {
                                            required property var modelData

                                            link: modelData
                                            onActivated: target => screen.navigate(target, true)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Label {
                text: screen.game.gamepad.active ? qsTr("%1 on a name or an item opens its page. %2 closes.").arg(screen.game.gamepad.pads.texts.south).arg(screen.game.gamepad.pads.texts.east)
                      : qsTr("Click a name or an item to open its page. Backspace goes back, Esc or %1 closes.").arg(screen.game.settings.keys.labels.handbook)
                color: Theme.textMuted
            }
        }
    }
}

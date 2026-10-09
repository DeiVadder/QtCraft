pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The handbook: categories on the left, the entries of a category (or the search results) in the
// middle and the open page on the right. Links and recipe cells open other pages; Back returns.
// Esc or F1 closes it and gives the keyboard back to the screen below.
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
    readonly property var listed: !visible || game.handbook.categories.length === 0 ? []
                                  : (searching ? game.handbook.search(searchField.text) : game.handbook.entries(category))

    visible: game.handbookOpen
    color: "#b0000000"
    focus: visible

    function navigate(target, remember) {
        const id = game.handbook.resolve(target);
        if (id.length === 0 || id === current)
            return;
        if (remember && current.length > 0)
            history = history.concat([current]);
        current = id;
        page = game.handbook.page(id);
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
        game.closeHandbook();
    }

    onVisibleChanged: {
        if (visible) {
            focusBefore = Window.activeFocusItem;
            history = [];
            searchField.text = game.handbookQuery;
            const found = game.handbookQuery.length > 0 ? game.handbook.search(game.handbookQuery)[0] : undefined;
            if (game.handbookTarget.length > 0) {
                navigate(game.handbookTarget, false);
            } else if (found) {
                navigate(found.id, false);
            } else if (current.length === 0) {
                const first = game.handbook.entries("playing").find(row => row.id !== undefined);
                if (first)
                    navigate(first.id, false);
            } else {
                // The handbook was rebuilt (language, mods): show the same page again.
                const shown = current;
                current = "";
                navigate(shown, false);
            }
            if (game.handbookQuery.length > 0)
                searchField.forceActiveFocus();
            else
                forceActiveFocus();
        } else if (focusBefore) {
            focusBefore.forceActiveFocus();
            focusBefore = null;
        }
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_F1) {
            screen.close();
            event.accepted = true;
        } else if (event.key === Qt.Key_Backspace || (event.key === Qt.Key_Left && (event.modifiers & Qt.AltModifier))) {
            screen.back();
            event.accepted = true;
        }
    }

    // Swallows clicks so nothing below reacts.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: wheel => wheel.accepted = true
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 20
        radius: 6
        color: "#262626"
        border.color: "#80ffffff"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Button {
                    text: qsTr("‹ Back")
                    enabled: screen.history.length > 0
                    onClicked: screen.back()
                }

                Label {
                    text: qsTr("Handbook")
                    color: "white"
                    font.pixelSize: 24
                    font.bold: true
                }

                Item {
                    Layout.fillWidth: true
                }

                TextField {
                    id: searchField

                    Layout.preferredWidth: 320
                    placeholderText: qsTr("Search blocks, items, creatures …")
                    selectByMouse: true
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Escape || event.key === Qt.Key_F1) {
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
                spacing: 12

                ListView {
                    id: categoryList

                    Layout.preferredWidth: 190
                    Layout.fillHeight: true
                    clip: true
                    spacing: 1
                    model: screen.game.handbook.categories

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
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: categoryRow.modelData.count
                            color: "#90ffffff"
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    color: "#40ffffff"
                }

                ListView {
                    id: entryList

                    // Highlights the open page in the list and scrolls it into view.
                    function select() {
                        const index = screen.listed.findIndex(row => row.id === screen.current);
                        currentIndex = index;
                        if (index >= 0)
                            positionViewAtIndex(index, ListView.Contain);
                    }

                    Layout.preferredWidth: 270
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
                        color: "#c0ffffff"
                    }

                    delegate: ItemDelegate {
                        id: entryRow

                        required property var modelData
                        required property int index
                        readonly property bool header: modelData.header !== undefined

                        width: ListView.view.width
                        height: header ? 30 : 36
                        enabled: !header
                        highlighted: !header && modelData.id === screen.current
                        onClicked: screen.navigate(modelData.id, true)

                        contentItem: RowLayout {
                            spacing: 8

                            Image {
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24
                                visible: !entryRow.header
                                source: !entryRow.header && entryRow.modelData.icon.length > 0 ? "image://items/" + entryRow.modelData.icon : ""
                                sourceSize: Qt.size(24, 24)
                                smooth: false
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0

                                Label {
                                    Layout.fillWidth: true
                                    text: entryRow.header ? entryRow.modelData.header : entryRow.modelData.title
                                    color: entryRow.header ? "#ffd27a" : "white"
                                    font.bold: entryRow.header
                                    elide: Text.ElideRight
                                }

                                Label {
                                    visible: screen.searching && !entryRow.header
                                    text: entryRow.header ? "" : entryRow.modelData.categoryTitle
                                    color: "#90ffffff"
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    color: "#40ffffff"
                }

                Flickable {
                    id: pageView

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: width
                    contentHeight: pageColumn.implicitHeight + 20
                    boundsBehavior: Flickable.StopAtBounds

                    ScrollBar.vertical: ScrollBar {}

                    ColumnLayout {
                        id: pageColumn

                        width: pageView.width - 16
                        spacing: 12

                        RowLayout {
                            spacing: 12

                            Rectangle {
                                Layout.preferredWidth: 56
                                Layout.preferredHeight: 56
                                visible: (screen.page.icon || "").length > 0
                                color: "#a0303030"
                                radius: 3

                                Image {
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    source: parent.visible ? "image://items/" + screen.page.icon : ""
                                    sourceSize: Qt.size(44, 44)
                                    smooth: false
                                }
                            }

                            ColumnLayout {
                                spacing: 2

                                Label {
                                    text: screen.page.title || ""
                                    color: "white"
                                    font.pixelSize: 26
                                    font.bold: true
                                }

                                Label {
                                    text: (screen.page.categoryTitle || "") + "  ·  " + (screen.page.packName || "")
                                    color: "#a0ffffff"
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: screen.page.text || ""
                            color: "#f0ffffff"
                            font.pixelSize: 15
                            wrapMode: Text.Wrap
                            textFormat: Text.MarkdownText
                        }

                        // Facts as a two-column table.
                        Rectangle {
                            Layout.fillWidth: true
                            visible: factRepeater.count > 0
                            implicitHeight: factColumn.implicitHeight + 12
                            color: "#20ffffff"
                            radius: 4

                            ColumnLayout {
                                id: factColumn

                                x: 8
                                y: 6
                                width: parent.width - 16
                                spacing: 4

                                Repeater {
                                    id: factRepeater

                                    model: screen.page.facts || []

                                    RowLayout {
                                        id: factRow

                                        required property var modelData

                                        Layout.fillWidth: true
                                        spacing: 10

                                        Label {
                                            Layout.preferredWidth: 210
                                            Layout.alignment: Qt.AlignTop
                                            topPadding: 4
                                            text: factRow.modelData.label
                                            color: "#b8ffffff"
                                            wrapMode: Text.Wrap
                                        }

                                        Flow {
                                            Layout.fillWidth: true
                                            spacing: 6

                                            Label {
                                                visible: text.length > 0
                                                topPadding: 4
                                                width: Math.min(implicitWidth, parent.width)
                                                text: factRow.modelData.value
                                                color: "white"
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
                                spacing: 6

                                Label {
                                    visible: text.length > 0
                                    text: sectionColumn.modelData.title
                                    color: "#ffd27a"
                                    font.pixelSize: 18
                                    font.bold: true
                                }

                                Label {
                                    Layout.fillWidth: true
                                    visible: text.length > 0
                                    text: sectionColumn.modelData.text
                                    color: "#f0ffffff"
                                    font.pixelSize: 15
                                    wrapMode: Text.Wrap
                                    textFormat: Text.MarkdownText
                                }

                                Flow {
                                    Layout.fillWidth: true
                                    visible: sectionColumn.modelData.recipes.length > 0
                                    spacing: 18

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
                                    spacing: 6

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
                text: qsTr("Click a name or an item to open its page. Backspace goes back, Esc or F1 closes.")
                color: "#a0ffffff"
            }
        }
    }
}

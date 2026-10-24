pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// A screen one level down the side bar (drill-down, docs/UI-Leitfaden.md 8b, decision E-852): the
// settings and the handbook. The side bar keeps its place and the logo; its entries are a back
// entry ("‹ Main menu", "‹ Pause menu", "‹ Back to game"), the screen's title and its categories;
// the chosen category shows on the right, in the menu's margin and no wider than a large screen
// (centred in the room beside the bar when the window is wider).
// In the main menu it lies on the menu's background, in the game on the nearly opaque panel, so
// neither the world nor the HUD shows through the text. The same screen serves both.
FocusScope {
    id: screen

    // Over the world (panel) rather than in the main menu (its background).
    property bool inGame: false
    // The back entry's text, naming where it leads.
    property string backText
    property string title
    // Another screen (the handbook) lies over this one: the bar and the content hide, so Tab and
    // the pad do not reach them below it.
    property bool covered: false
    // Room kept free below the content, for the pad's hints (PadHints) while they show.
    property real bottomRoom: 0
    // The categories below the title (in a ColumnLayout that fills the rest of the bar).
    property alias entries: entryColumn.data
    // The content on the right.
    default property alias content: contentArea.data
    readonly property NavButton backEntry: backButton

    // The back entry was chosen (Esc and the pad's B too, where the screen passes them on).
    signal backRequested

    // The menu's background behind the bar and the content, so nothing of the main menu shows.
    Rectangle {
        anchors.fill: parent
        visible: !screen.inGame
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.menuTop }
            GradientStop { position: 1.0; color: Theme.menuBottom }
        }
    }
    Rectangle {
        anchors.fill: parent
        visible: screen.inGame
        color: Theme.panel
    }

    // Swallows clicks so nothing below reacts; nothing a screen reader offers.
    MouseArea {
        anchors.fill: parent
        Accessible.ignored: true
        acceptedButtons: Qt.AllButtons
        onWheel: wheel => wheel.accepted = true
    }

    SideBar {
        id: bar

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Ui.size.menuNav
        visible: !screen.covered

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Ui.space.s

            NavButton {
                id: backButton

                objectName: "sidebarBack"
                text: screen.backText
                onClicked: screen.backRequested()
            }

            Label {
                Layout.fillWidth: true
                Layout.topMargin: Ui.space.s
                Layout.leftMargin: Ui.space.l
                Layout.rightMargin: Ui.space.s
                text: screen.title
                font.pixelSize: Ui.type.section
                font.bold: true
                color: Theme.text
                elide: Text.ElideRight
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }

            ColumnLayout {
                id: entryColumn

                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Ui.space.s
            }
        }
    }

    Item {
        id: contentArea

        visible: !screen.covered
        // In the middle of the room beside the bar when the window is wider than a large screen.
        x: Ui.pageX(bar.width, screen.width, width)
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: Ui.space.xl
        anchors.bottomMargin: Ui.space.l + screen.bottomRoom
        width: Ui.pageWidth(bar.width, screen.width)
    }
}

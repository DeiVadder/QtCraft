// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

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
    // The shown page: focusPage() puts the focus on its first item (the first setting).
    property Item shownPage: null
    // The shown page can be reset to its defaults with the pad's Y (PadHints shows it).
    property bool resettable: false
    // The pad's first Y asked to confirm the reset: a second Y within resetConfirmMs resets, any
    // other command, a page switch or the time running out cancels (decision E-925).
    readonly property bool resetArmed: resetTimer.running
    readonly property int resetConfirmMs: 3000
    // A short notice over the content (the reset's question and result), shown for noticeMs.
    property string notice
    readonly property int noticeMs: 3000
    // The page's first item, waiting to be laid out before it takes the focus (focusPage()).
    property Item pendingFirst: null

    // The back entry was chosen (Esc and the pad's B too, where the screen passes them on).
    signal backRequested

    // The pad's Y on a resettable page: true when it confirms a reset asked for within
    // resetConfirmMs, false when it only asks (the screen then shows its question).
    function confirmReset(): bool {
        if (resetArmed) {
            resetTimer.stop();
            return true;
        }
        resetTimer.restart();
        return false;
    }

    function cancelReset() {
        resetTimer.stop();
    }

    // Shows `text` over the content for noticeMs.
    function showNotice(text: string) {
        notice = text;
        noticeTimer.restart();
    }

    // The first shown and enabled item in `item`'s tree that Tab reaches, in tree order (the
    // pages lay their settings out top to bottom); null for none.
    function firstTabItem(item: Item): Item {
        for (let index = 0; index < item.children.length; ++index) {
            const child = item.children[index];
            if (!child.visible || !child.enabled)
                continue;
            if (child.activeFocusOnTab)
                return child;
            const inner = firstTabItem(child);
            if (inner)
                return inner;
        }
        return null;
    }

    // Puts the focus on the first item of `shownPage` that Tab reaches: on opening and after the
    // category changed, so the pad's first press never lands on the back entry
    // (docs/UI-Leitfaden.md 10). A page shown for the first time is laid out on the next frame (the
    // pad passes over items without a size): until its first item has one the page holds the focus
    // (the pad may meanwhile show its focus where it starts), then that item takes it, while the
    // focus is still on this screen. A page with nothing to focus (the players with only the host)
    // gives it to its entry in the side bar, never to the back entry.
    function focusPage() {
        const first = shownPage ? firstTabItem(shownPage) : null;
        pendingFirst = null;
        if (first && first.width > 0 && first.height > 0) {
            first.forceActiveFocus(Qt.TabFocusReason);
            return;
        }

        const entry = first ? null : currentNavEntry();
        if (entry) {
            entry.forceActiveFocus(Qt.TabFocusReason);
            return;
        }

        shownPage?.forceActiveFocus(Qt.TabFocusReason);
        pendingFirst = first;
    }

    // The chosen entry of the side bar's category list, null without one.
    function currentNavEntry(): Item {
        for (let index = 0; index < entryColumn.children.length; ++index) {
            const child = entryColumn.children[index];
            if (child instanceof NavList)
                return (child as NavList).currentEntry();
        }
        return null;
    }

    // Whether `item` lies on the shown page.
    function onShownPage(item: Item): bool {
        for (let parent = item?.parent; parent; parent = parent.parent) {
            if (parent === shownPage)
                return true;
        }
        return false;
    }

    // The item waiting in focusPage() got its size: it takes the focus while it is still the one
    // waiting, on the shown page, and the focus is still on this screen.
    function focusPending() {
        const first = pendingFirst;
        if (!first || first.width <= 0 || first.height <= 0)
            return;

        pendingFirst = null;
        if (first.visible && onShownPage(first) && screen.activeFocus)
            first.forceActiveFocus(Qt.TabFocusReason);
    }

    onShownPageChanged: cancelReset()
    onVisibleChanged: cancelReset()

    Connections {
        target: screen.pendingFirst

        function onWidthChanged() {
            screen.focusPending();
        }

        function onHeightChanged() {
            screen.focusPending();
        }
    }

    Timer {
        id: resetTimer

        interval: screen.resetConfirmMs
    }

    Timer {
        id: noticeTimer

        interval: screen.noticeMs
    }

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
                // A pad's first press or A must not leave the screen: B goes back, Up from the first
                // category (or Left) reaches it, it is never where the pad's focus starts.
                padLeftOnly: true
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

    // The notice, at the foot of the content above the pad's hints.
    Rectangle {
        anchors.horizontalCenter: contentArea.horizontalCenter
        anchors.bottom: contentArea.bottom
        width: Math.min(noticeLabel.implicitWidth + 2 * Ui.space.l, contentArea.width)
        height: noticeLabel.implicitHeight + 2 * Ui.space.s
        visible: noticeTimer.running && !screen.covered
        radius: Ui.radius
        color: Theme.hudBackdrop

        Label {
            id: noticeLabel

            objectName: "sidebarNotice"
            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.width - 2 * Ui.space.l)
            text: screen.notice
            color: Theme.text
            font.pixelSize: Ui.type.body
            wrapMode: Text.Wrap
            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
        }
    }
}

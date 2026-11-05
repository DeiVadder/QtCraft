// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// A message over the whole window after a world could not start or save (Main.qml,
// GameController.statusText), on ScreenFrame like every screen; "Back to menu" or "Cancel" below it
// when there is a way back. --overlay message:<text> shows one for screenshots.
Rectangle {
    id: screen

    // The key table (GameController.settings.keys): Esc goes back through it.
    required property KeyControls keys
    property alias text: message.text
    property bool showBackButton
    property string backText: qsTr("Back to menu")

    signal backRequested

    color: Theme.scrimStrong
    focus: visible
    onVisibleChanged: if (visible) forceActiveFocus()
    Keys.onPressed: event => {
        if (screen.showBackButton && screen.keys.screenKey(event.key, event.isAutoRepeat, KeyControls.ClosingKey.EscOnly) === KeyControls.ScreenKey.Close)
            screen.backRequested();
        event.accepted = event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab;
    }

    ScreenFrame {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Ui.screen.margin, Ui.screen.s)
        height: column.implicitHeight + 2 * Ui.space.xl

        ColumnLayout {
            id: column

            anchors.fill: parent
            anchors.margins: Ui.space.xl
            spacing: Ui.space.xl

            Label {
                id: message

                Layout.fillWidth: true
                // The text may hold a server's reason.
                textFormat: Text.PlainText
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: Theme.text
                font.pixelSize: Ui.type.section
            }

            GameButton {
                objectName: "messageBackButton"
                Layout.fillWidth: true
                large: true
                visible: screen.showBackButton
                text: screen.backText
                onClicked: screen.backRequested()
            }
        }
    }
}

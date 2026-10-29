// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// Asks before leaving a screen or page with unsaved changes (decision E-852, docs/UI-Leitfaden.md
// 5, pattern "edit, then save or discard"): "Save", "Discard" or "Keep editing". Keep editing is the
// safe default and has the focus; Esc and the pad's B keep editing too. `ask(proceed)` opens it and
// calls `proceed` after Save or Discard (saveChosen() and discardChosen() say which first).
ModalDialog {
    id: dialog

    // What the leave was for, run after Save or Discard.
    property var proceed: null

    signal saveChosen
    signal discardChosen

    function ask(next: var) {
        proceed = next;
        open();
    }

    function finish(save: bool) {
        const next = proceed;
        proceed = null;
        close();
        if (save)
            saveChosen();
        else
            discardChosen();
        if (next)
            next();
    }

    closePolicy: Popup.CloseOnEscape
    // A tab focus shows the outline (GameButton draws it on visualFocus).
    onOpened: keepButton.forceActiveFocus(Qt.TabFocusReason)
    onClosed: proceed = null

    header: Label {
        leftPadding: Ui.space.xl
        rightPadding: Ui.space.xl
        topPadding: Ui.space.xl
        text: qsTr("Unsaved changes")
        font.pixelSize: Ui.type.title
        font.bold: true
        color: Theme.text
        Accessible.role: Accessible.Heading
        Accessible.name: text
    }

    Label {
        width: dialog.availableWidth
        wrapMode: Text.Wrap
        text: qsTr("Save your changes before you leave, or discard them?")
        font.pixelSize: Ui.type.body
        color: Theme.text
    }

    footer: RowLayout {
        spacing: Ui.space.s

        Item {
            Layout.fillWidth: true
        }
        GameButton {
            objectName: "discardChangesButton"
            Layout.bottomMargin: Ui.space.xl
            kind: GameButton.Destructive
            text: qsTr("Discard")
            onClicked: dialog.finish(false)
        }
        GameButton {
            id: keepButton

            objectName: "keepEditingButton"
            Layout.bottomMargin: Ui.space.xl
            text: qsTr("Keep editing")
            onClicked: dialog.close()
        }
        GameButton {
            objectName: "saveChangesButton"
            Layout.bottomMargin: Ui.space.xl
            Layout.rightMargin: Ui.space.xl
            kind: GameButton.Primary
            // The focus starts on "Keep editing", not here.
            highlighted: false
            text: qsTr("Save")
            onClicked: dialog.finish(true)
        }
    }
}

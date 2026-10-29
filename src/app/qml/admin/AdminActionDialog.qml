import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// Asks before kicking or banning a player or stopping the server (decision E-863): modal for the
// whole window (ModalDialog), with what will happen, an optional reason for the player, "Cancel"
// (where the focus starts) and the action named after itself (destructive). `ask()` opens it and
// calls `proceed(reason)` when the action is chosen.
ModalDialog {
    id: dialog

    property string heading
    property string message
    property string actionText
    // The dialog takes a reason the player reads.
    property bool withReason: false
    // The longest reason the server takes (AdminController.reasonLength).
    property int reasonLength
    property var proceed: null

    // Takes the operator rights of `name`; the player's own only after asking (managing ends then).
    function takeRights(admin: AdminController, name: string) {
        if (!admin.isOwnName(name)) {
            admin.deop(name);
            return;
        }
        ask(qsTr("Take your own rights"),
            qsTr("You are no operator afterwards and cannot manage this server until another operator makes you one again."),
            qsTr("Take rights"), false, () => admin.deop(name));
    }

    function ask(title: string, text: string, action: string, reason: bool, next: var) {
        heading = title;
        message = text;
        actionText = action;
        withReason = reason;
        proceed = next;
        reasonField.text = "";
        open();
    }

    onOpened: cancelButton.forceActiveFocus(Qt.TabFocusReason)
    onAccepted: {
        const next = proceed;
        proceed = null;
        if (next)
            next(reasonField.text.trim());
    }
    onRejected: proceed = null

    header: Label {
        leftPadding: Ui.space.xl
        rightPadding: Ui.space.xl
        topPadding: Ui.space.xl
        text: dialog.heading
        font.pixelSize: Ui.type.title
        font.bold: true
        color: Theme.text
        elide: Text.ElideRight
        Accessible.role: Accessible.Heading
        Accessible.name: text
    }

    ColumnLayout {
        width: dialog.availableWidth
        spacing: Ui.space.m

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            text: dialog.message
            font.pixelSize: Ui.type.body
            color: Theme.text
        }
        Label {
            visible: dialog.withReason
            text: qsTr("Reason")
            font.pixelSize: Ui.type.help
            color: Theme.textSecondary
        }
        GameTextField {
            id: reasonField

            objectName: "adminReasonField"
            Layout.fillWidth: true
            visible: dialog.withReason
            maximumLength: dialog.reasonLength
            placeholderText: qsTr("Optional, the player reads it")
            Accessible.name: qsTr("Reason")
            onAccepted: dialog.accept()
        }
    }

    footer: RowLayout {
        spacing: Ui.space.s

        Item {
            Layout.fillWidth: true
        }
        GameButton {
            id: cancelButton

            objectName: "adminDialogCancel"
            Layout.bottomMargin: Ui.space.xl
            text: qsTr("Cancel")
            onClicked: dialog.reject()
        }
        GameButton {
            objectName: "adminDialogConfirm"
            Layout.bottomMargin: Ui.space.xl
            Layout.rightMargin: Ui.space.xl
            kind: GameButton.Destructive
            text: dialog.actionText
            onClicked: dialog.accept()
        }
    }
}

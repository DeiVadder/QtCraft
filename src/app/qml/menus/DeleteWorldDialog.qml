import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// Asks before a saved world is deleted (decision E-852): the world's name, and the buttons named
// after what they do ("Delete", destructive; "Cancel", where the focus starts).
ModalDialog {
    id: dialog

    property string folder
    property string worldName

    // A tab focus shows the outline (GameButton draws it on visualFocus).
    onOpened: cancelButton.forceActiveFocus(Qt.TabFocusReason)

    header: Label {
        leftPadding: Ui.space.xl
        rightPadding: Ui.space.xl
        topPadding: Ui.space.xl
        text: qsTr("Delete world")
        font.pixelSize: Ui.type.title
        font.bold: true
        color: Theme.text
        Accessible.role: Accessible.Heading
        Accessible.name: text
    }

    Label {
        objectName: "deleteWorldMessage"
        width: dialog.availableWidth
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
        text: qsTr("Delete “%1” permanently? This cannot be undone.").arg(dialog.worldName)
        font.pixelSize: Ui.type.body
        color: Theme.text
    }

    footer: RowLayout {
        spacing: Ui.space.s

        Item {
            Layout.fillWidth: true
        }
        GameButton {
            id: cancelButton

            Layout.bottomMargin: Ui.space.xl
            text: qsTr("Cancel")
            onClicked: dialog.reject()
        }
        GameButton {
            Layout.bottomMargin: Ui.space.xl
            Layout.rightMargin: Ui.space.xl
            kind: GameButton.Destructive
            text: qsTr("Delete")
            onClicked: dialog.accept()
        }
    }
}

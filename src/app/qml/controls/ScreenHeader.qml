import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The head of a screen over the world (docs/UI-Leitfaden.md 5, rule 1 of Menue-Pruefung E.0): the
// title on the left, "Close" with its key as a plate on the right, clickable. No "×", no "Done":
// what the screen changes holds already, closing only leaves it.
RowLayout {
    id: header

    property alias title: titleLabel.text
    // The pad's back button as the player reads it while the pad leads; empty: the keyboard's Esc.
    property string padKey: ""
    // The key that closes the screen as the plate shows it.
    readonly property string closeKey: padKey.length > 0 ? padKey : qsTr("Esc")
    // The close button's object name, for tests and the --overlay click: hook.
    property alias closeName: closeButton.objectName

    signal closeRequested

    spacing: Ui.space.m

    Label {
        id: titleLabel

        Layout.fillWidth: true
        color: Theme.text
        font.pixelSize: Ui.type.title
        font.bold: true
        elide: Text.ElideRight
        Accessible.role: Accessible.Heading
        Accessible.name: text
    }
    GameButton {
        id: closeButton

        kind: GameButton.Quiet
        text: qsTr("Close")
        keyHint: header.closeKey
        onClicked: header.closeRequested()
    }
}

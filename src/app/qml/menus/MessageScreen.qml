import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

Rectangle {
    id: screen

    property alias text: message.text
    property bool showBackButton
    property string backText: qsTr("Back to menu")

    signal backRequested

    color: Theme.scrimStrong
    focus: visible
    onVisibleChanged: if (visible) forceActiveFocus()
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape && screen.showBackButton)
            screen.backRequested();
        event.accepted = event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab;
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - Ui.px(32), Ui.px(520))
        spacing: Ui.px(16)

        Label {
            id: message

            Layout.fillWidth: true
            // The text may hold a server's reason.
            textFormat: Text.PlainText
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: Theme.text
            font.pixelSize: Ui.px(22)
        }

        Button {
            Layout.alignment: Qt.AlignHCenter
            visible: screen.showBackButton
            text: screen.backText
            onClicked: screen.backRequested()
        }
    }
}

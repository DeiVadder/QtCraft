import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// One recently joined server in the multiplayer screen: its name and address; a click puts the
// address into the field, Join joins at once, × removes the entry.
Rectangle {
    id: row

    required property var modelData

    signal chosen(string address)
    signal joinRequested(string address)
    signal removeRequested(string address)

    height: Ui.px(56)
    activeFocusOnTab: true
    radius: Ui.px(4)
    color: hover.hovered ? Theme.rowHighlight : Theme.scrim
    Accessible.role: Accessible.ListItem
    Accessible.name: row.modelData.name.length > 0 ? qsTr("%1 (%2)").arg(row.modelData.name).arg(row.modelData.address) : row.modelData.address

    HoverHandler {
        id: hover
    }

    TapHandler {
        onTapped: row.chosen(row.modelData.address)
        onDoubleTapped: row.joinRequested(row.modelData.address)
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Ui.px(8)
        spacing: Ui.px(8)

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Ui.px(2)

            Label {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: row.modelData.name.length > 0 ? row.modelData.name : row.modelData.address
                color: Theme.text
                font.bold: true
                elide: Text.ElideRight
            }

            Label {
                Layout.fillWidth: true
                // Without a server name the address is the title already.
                readonly property string place: row.modelData.name.length > 0 ? row.modelData.address : ""
                visible: text.length > 0
                textFormat: Text.PlainText
                text: row.modelData.lastJoined.length === 0 ? place
                      : place.length > 0 ? qsTr("%1 · last joined %2").arg(place).arg(row.modelData.lastJoined)
                      : qsTr("last joined %1").arg(row.modelData.lastJoined)
                color: Theme.textSecondary
                elide: Text.ElideRight
            }
        }

        Button {
            text: qsTr("Join")
            onClicked: row.joinRequested(row.modelData.address)
        }

        Button {
            id: removeButton

            text: "×"
            Accessible.name: qsTr("Remove %1 from the list").arg(row.modelData.address)
            onClicked: row.removeRequested(row.modelData.address)

            ThemedToolTip {
                shown: removeButton.hovered
                text: qsTr("Remove from the list")
            }
        }
    }
}

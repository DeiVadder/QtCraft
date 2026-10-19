import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The player's profile as others will see it: a live preview of the character, the name and a
// button to the character screen (decision E-231). Shown in the main menu; the join screen of the
// multiplayer (stage 25) uses it too.
Rectangle {
    id: card

    required property GameController game
    // The name the game joins network games with (set in the multiplayer screen, decision E-258).
    readonly property string playerName: game.multiplayer.settings.playerName.length > 0 ? game.multiplayer.settings.playerName : qsTr("Player")
    // Another screen covers the card: its preview stops moving.
    property bool covered: false

    signal customizeRequested

    implicitHeight: 168
    radius: 4
    color: Theme.scrim

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 12

        Rectangle {
            Layout.preferredWidth: 104
            Layout.fillHeight: true
            radius: 4
            color: Theme.panelRaised

            CharacterPreviewHost {
                previewName: "profilePreview"
                anchors.fill: parent
                anchors.margins: 2
                source: card.game.preview
                // A gentle idle motion is enough in the menu.
                framesPerSecond: 15
                paused: card.covered
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 6

            Label {
                text: qsTr("Your character")
                color: Theme.text
                font.pixelSize: 17
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: card.playerName
                color: Theme.textSecondary
                elide: Text.ElideRight
            }
            Button {
                text: qsTr("Customize…")
                onClicked: card.customizeRequested()
            }
        }
    }
}

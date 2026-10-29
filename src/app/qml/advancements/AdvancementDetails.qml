pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// What the chosen advancement of the tree asks for (decision E-924): icon and title, its frame and
// how far the player is, the description, the criteria with met or not (a mark and the colour), and
// the way to its handbook page. Without a chosen one it says how to choose one. Scrolls when the
// criteria are many.
Rectangle {
    id: details

    // An entry of AdvancementTree.nodes, or undefined.
    property var advancement

    signal handbookRequested(string target)

    readonly property bool shown: advancement !== undefined && advancement !== null

    function frameText(frame: int): string {
        switch (frame) {
        case AdvancementTree.Goal:
            return qsTr("Goal");
        case AdvancementTree.Challenge:
            return qsTr("Challenge");
        default:
            return qsTr("Task");
        }
    }

    function stateText(): string {
        if (advancement.state === AdvancementTree.Reached)
            return qsTr("Reached");
        if (advancement.progress.length > 0)
            return qsTr("%1 reached").arg(advancement.progress);
        return qsTr("Not reached yet");
    }

    objectName: "advancementDetails"
    color: Theme.panelRaised
    radius: Ui.radius

    Label {
        anchors.fill: parent
        anchors.margins: Ui.space.l
        visible: !details.shown
        text: qsTr("Point at an advancement or move the focus to it to see what it asks for.")
        color: Theme.textMuted
        font.pixelSize: Ui.type.help
        wrapMode: Text.Wrap
    }

    Flickable {
        id: scroller

        anchors.fill: parent
        anchors.margins: Ui.space.l
        visible: details.shown
        clip: true
        contentWidth: width
        contentHeight: column.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ThemedScrollBar {}

        ColumnLayout {
            id: column

            width: scroller.width
            spacing: Ui.space.s

            RowLayout {
                Layout.fillWidth: true
                spacing: Ui.space.s

                Image {
                    Layout.preferredWidth: Ui.size.handbookIcon
                    Layout.preferredHeight: Ui.size.handbookIcon
                    Layout.alignment: Qt.AlignTop
                    source: details.shown ? ItemIcons.source(details.advancement.icon, ItemIcons.generation) : ""
                    sourceSize: Qt.size(32, 32)
                    smooth: false
                }
                Label {
                    objectName: "advancementTitle"
                    Layout.fillWidth: true
                    text: details.shown ? details.advancement.title : ""
                    color: Theme.text
                    font.pixelSize: Ui.type.section
                    font.bold: true
                    wrapMode: Text.Wrap
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
            }

            Label {
                objectName: "advancementState"
                Layout.fillWidth: true
                text: details.shown ? qsTr("%1 · %2").arg(details.frameText(details.advancement.frame)).arg(details.stateText()) : ""
                color: details.shown && details.advancement.state === AdvancementTree.Reached ? Theme.accent : Theme.heading
                font.pixelSize: Ui.type.help
                font.bold: true
                wrapMode: Text.Wrap
            }

            Label {
                Layout.fillWidth: true
                text: details.shown ? details.advancement.description : ""
                color: Theme.textSecondary
                font.pixelSize: Ui.type.body
                wrapMode: Text.Wrap
            }

            Label {
                Layout.fillWidth: true
                Layout.topMargin: Ui.space.s
                visible: criteria.count > 0
                text: qsTr("Criteria")
                color: Theme.heading
                font.pixelSize: Ui.type.group
                font.bold: true
            }

            Repeater {
                id: criteria

                model: details.shown ? details.advancement.criteria : []

                delegate: Label {
                    required property var modelData

                    Layout.fillWidth: true
                    // A mark and the colour: met or not reads without colour too.
                    text: (modelData.met ? "✓ " : "○ ") + modelData.text
                    color: modelData.met ? Theme.text : Theme.textMuted
                    font.pixelSize: Ui.type.body
                    wrapMode: Text.Wrap
                    Accessible.name: modelData.met ? qsTr("%1: met").arg(modelData.text) : qsTr("%1: not met").arg(modelData.text)
                }
            }

            GameButton {
                objectName: "advancementHandbook"
                Layout.fillWidth: true
                Layout.topMargin: Ui.space.s
                visible: details.shown && details.advancement.handbook.length > 0
                text: qsTr("Open in the handbook")
                onClicked: details.handbookRequested(details.advancement.handbook)
            }
        }
    }
}

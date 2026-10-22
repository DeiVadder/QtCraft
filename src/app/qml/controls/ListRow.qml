import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// A row of a list of things (decision E-845, docs/UI-Leitfaden.md 6.9): worlds, servers, mods,
// texture packs. A picture on the left (optional), the title in bold and a subtitle, further lines
// below them, and the row's actions on the right. Texts are plain (names and descriptions come from
// files and the network).
Rectangle {
    id: row

    property alias title: titleLabel.text
    property alias subtitle: subtitleLabel.text
    // The picture on the left, the further lines under the subtitle, the actions on the right.
    property alias leading: leadingSlot.data
    default property alias details: detailColumn.data
    property alias actions: actionColumn.data

    Layout.fillWidth: true
    implicitHeight: layout.implicitHeight + 2 * Ui.space.s
    radius: Ui.radius
    color: Theme.panelRaised

    RowLayout {
        id: layout

        x: Ui.space.m
        y: Ui.space.s
        width: parent.width - 2 * Ui.space.m
        spacing: Ui.space.m

        Item {
            id: leadingSlot

            Layout.alignment: Qt.AlignTop
            visible: children.length > 0
            implicitWidth: childrenRect.width
            implicitHeight: childrenRect.height
        }

        // Takes the width the picture and the actions leave (wrapped texts do not widen it).
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            Layout.alignment: Qt.AlignTop
            spacing: Ui.space.xs

            Label {
                id: titleLabel

                Layout.fillWidth: true
                textFormat: Text.PlainText
                font.pixelSize: Ui.type.body
                font.bold: true
                color: Theme.text
                elide: Text.ElideRight
            }
            Label {
                id: subtitleLabel

                Layout.fillWidth: true
                visible: text.length > 0
                textFormat: Text.PlainText
                font.pixelSize: Ui.type.help
                color: Theme.textSecondary
                wrapMode: Text.Wrap
            }
            ColumnLayout {
                id: detailColumn

                Layout.fillWidth: true
                spacing: Ui.space.xs
            }
        }

        ColumnLayout {
            id: actionColumn

            Layout.alignment: Qt.AlignTop
            spacing: Ui.space.xs
        }
    }
}

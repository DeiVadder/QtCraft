import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// One page of the settings (decision E-845, docs/UI-Leitfaden.md 5): its title and its groups one
// below the other; the page scrolls as a whole while the screen's category list and footer stay.
Flickable {
    id: page

    property alias title: heading.text
    default property alias groups: column.data

    clip: true
    contentWidth: width
    contentHeight: column.implicitHeight
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ThemedScrollBar {}

    ColumnLayout {
        id: column

        // A gutter on the right for the scroll bar.
        width: page.width - Ui.space.l
        spacing: Ui.space.xl

        Label {
            id: heading

            font.pixelSize: Ui.type.section
            font.bold: true
            color: Theme.text
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }
    }
}

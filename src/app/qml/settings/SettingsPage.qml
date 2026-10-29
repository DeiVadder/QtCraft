import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// One page of the settings (decision E-845, docs/UI-Leitfaden.md 5): its title and its groups one
// below the other; the page scrolls as a whole while the screen's category list and footer stay. A
// page with defaults to go back to sets `resettable` and handles resetRequested().
Flickable {
    id: page

    property alias title: heading.text
    default property alias groups: column.data
    // The page has defaults to go back to: the pad's Y asks for them (resetRequested()), as the
    // page's reset button does.
    property bool resettable: false
    // The page's reset button: the pad's focus goes there after a reset by the pad.
    property Item resetButton: null
    // What the page's last action did (a reset, a moved key); empty for nothing to say.
    property string message

    // Back to the defaults: from the page's reset button, or from the pad's Y (`byPad`: the focus
    // stays in view, the screen shows `message` as a notice).
    signal resetRequested(bool byPad)

    // Scrolls the page so that `item` (on the page) shows whole, with a small margin.
    function reveal(item: Item) {
        if (!item)
            return;

        const top = item.mapToItem(contentItem, 0, 0).y;
        if (top < contentY)
            contentY = Math.max(0, top - Ui.space.s);
        else if (top + item.height > contentY + height)
            contentY = Math.min(contentHeight - height, top + item.height - height + Ui.space.s);
    }

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

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtCraft

// The tooltip of every screen (decision E-279): text and background from the palette, nearly
// opaque, so it reads the same over panels, headings and the world. The style's tooltip takes the
// system's tooltip colours, which on macOS in the dark scheme are black text on a translucent
// light background. Screens declare it inside the item it explains instead of the attached
// ToolTip (test_ui_palette checks that). The popup exists only while it is shown: screens have
// hundreds of slots, and a popup per slot made them slower to create than the shared attached one.
Loader {
    id: holder

    property string text
    // Whether the tooltip shows, e.g. while the item is hovered.
    property bool shown
    // Whether the first line is a custom name (decision E-866), shown in italics.
    property bool italicTitle

    active: shown

    sourceComponent: ToolTip {
        id: tip

        parent: holder.parent
        visible: true
        text: holder.text

        contentItem: Column {
            Text {
                width: Math.min(implicitWidth, tip.availableWidth)
                text: tip.text.split("\n")[0]
                font.pixelSize: tip.font.pixelSize
                font.family: tip.font.family
                font.italic: holder.italicTitle
                color: Theme.text
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
            }

            Text {
                width: Math.min(implicitWidth, tip.availableWidth)
                visible: text !== ""
                text: tip.text.split("\n").slice(1).join("\n")
                font: tip.font
                color: Theme.text
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
            }
        }

        background: Rectangle {
            color: Theme.tooltipBackground
            border.color: Theme.panelBorder
            radius: Ui.radius
        }
    }
}

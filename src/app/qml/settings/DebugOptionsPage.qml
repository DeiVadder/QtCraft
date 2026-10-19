pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// Debug options (decision E-225, like Factorio's F4): the level of the debug display and the
// visualisations drawn into the world, each on its own; stored with the settings.
ColumnLayout {
    id: page

    required property GameController game

    spacing: 8

    RowLayout {
        Layout.fillWidth: true
        spacing: 12

        Label {
            text: qsTr("Debug display (%1)").arg(page.game.settings.keys.labels.debugOverlay)
        }
        ComboBox {
            id: levelBox

            objectName: "debugLevel"
            Layout.fillWidth: true
            model: [qsTr("Off"), qsTr("Compact: frame rate and frame time"), qsTr("Full: position, light, target, time breakdown")]
            currentIndex: page.game.settings.debug.level
            onActivated: index => page.game.settings.debug.level = index
        }
    }

    Label {
        text: qsTr("Shown in the world")
        font.bold: true
    }

    CheckBox {
        text: qsTr("Chunk and section borders")
        checked: page.game.settings.debug.chunkBorders
        onToggled: page.game.settings.debug.chunkBorders = checked
    }
    CheckBox {
        text: qsTr("Hitboxes of creatures, players and items")
        checked: page.game.settings.debug.hitboxes
        onToggled: page.game.settings.debug.hitboxes = checked
    }
    CheckBox {
        text: qsTr("Light values around the target (sky, block)")
        checked: page.game.settings.debug.lightValues
        onToggled: page.game.settings.debug.lightValues = checked
    }
    CheckBox {
        text: qsTr("Where creatures are heading")
        checked: page.game.settings.debug.mobGoals
        onToggled: page.game.settings.debug.mobGoals = checked
    }
    CheckBox {
        text: qsTr("Flash sections when they are meshed again")
        checked: page.game.settings.debug.remeshFlash
        onToggled: page.game.settings.debug.remeshFlash = checked
    }

    Item {
        Layout.fillHeight: true
    }

    Label {
        Layout.fillWidth: true
        textFormat: Text.PlainText
        text: qsTr("%1 opens these options in the world. Measured values also go to the log with --stats <ms>.")
                  .arg(page.game.settings.keys.labels.debugOptions)
        wrapMode: Text.Wrap
        opacity: 0.7
    }
}

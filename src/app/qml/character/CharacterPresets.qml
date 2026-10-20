pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// Presets of the character screen: built-in looks and the player's own, saved under a name
// (decision E-231). Choosing one takes its look at once.
GridLayout {
    id: presets

    // GameController.settings.character.
    required property var character

    columns: 3
    columnSpacing: Ui.px(8)

    Label {
        Layout.preferredWidth: Ui.px(110)
        text: qsTr("Preset")
    }
    ComboBox {
        id: presetBox

        Accessible.name: qsTr("Preset")
        Layout.fillWidth: true
        textRole: "label"
        model: presets.character.presets
        displayText: currentIndex < 0 ? qsTr("Choose a look") : currentText
        currentIndex: -1
        onActivated: index => presets.character.applyPreset(index)
    }
    Button {
        text: qsTr("Delete")
        enabled: presetBox.currentIndex >= 0 && presetBox.currentIndex < presets.character.presets.length
                 && !presets.character.presets[presetBox.currentIndex].builtIn
        onClicked: {
            presets.character.removePreset(presetBox.currentIndex);
            presetBox.currentIndex = -1;
        }
    }

    Item {
        Layout.preferredWidth: Ui.px(110)
    }
    TextField {
        id: presetName

        Accessible.name: qsTr("Preset name")
        Layout.fillWidth: true
        selectByMouse: true
        placeholderText: presets.character.canSavePreset ? qsTr("Name for a new preset") : qsTr("Name of a preset to replace")
        // At most 32 characters, counted as the game counts names (an emoji is one).
        onTextEdited: {
            const fitted = presets.character.fitPresetName(text);
            if (fitted !== text)
                text = fitted;
        }
    }
    Button {
        text: qsTr("Save preset")
        // Reads `presets`, so it is checked again when they change.
        enabled: presets.character.presets.length > 0 && presets.character.canSavePresetAs(presetName.text)
        onClicked: {
            if (presets.character.savePreset(presetName.text))
                presetName.text = "";
        }
    }

    Item {
        Layout.preferredWidth: Ui.px(110)
        visible: fullHint.visible
    }
    Label {
        id: fullHint

        Layout.columnSpan: 2
        Layout.fillWidth: true
        visible: !presets.character.canSavePreset
        text: qsTr("All %1 places for your own presets are taken: delete one, or save under the name of one to replace it.").arg(presets.character.maxPresets)
        wrapMode: Text.Wrap
        color: Theme.warning
    }
}

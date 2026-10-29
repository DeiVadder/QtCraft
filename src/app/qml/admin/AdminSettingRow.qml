import QtQuick
import QtQuick.Layouts
import QtCraft

// One game rule or server limit on the settings page of "Manage server" (decision E-863): its
// translated name, the key (the name /gamerule and /limits take) as a plate under it, the translated
// description, range and default in the player's language; a switch for on/off rules, a number field otherwise
// (any 64-bit value, checked by the controller), and "Default" to set it back. The value is the
// controller's draft (AdminSettingsPage).
SettingRow {
    id: row

    required property var modelData
    required property var settings
    readonly property real current: settings.valueOf(modelData)
    readonly property bool changed: current !== modelData.value

    // A field whose text is no value of the setting.
    property bool invalid: false

    label: changed ? qsTr("%1 (changed)").arg(modelData.name) : modelData.name
    // The key /gamerule and /limits take, as a plate under the name (a setting without a translated
    // name shows the key as its name already).
    badge: modelData.named ? modelData.key : ""
    help: modelData.boolean
          ? qsTr("%1 Default: %2.").arg(modelData.description).arg(modelData.defaultValue !== 0 ? qsTr("on") : qsTr("off"))
          : qsTr("%1 From %2 to %3, default %4.").arg(modelData.description).arg(modelData.lowText).arg(modelData.highText)
            .arg(modelData.defaultText)

    GameSwitch {
        id: toggle

        visible: row.modelData.boolean
        checked: row.current !== 0
        Accessible.name: row.modelData.name
        onToggled: {
            row.settings.change(row.modelData, checked ? 1 : 0);
            // The draft decides from now on, also after "Discard".
            checked = Qt.binding(() => row.current !== 0);
        }
    }
    GameTextField {
        id: field

        visible: !row.modelData.boolean
        Layout.preferredWidth: Ui.size.value
        horizontalAlignment: TextInput.AlignRight
        inputMethodHints: Qt.ImhDigitsOnly
        // Digits and a sign only; the range is the controller's (64-bit, never clamped).
        validator: RegularExpressionValidator {
            regularExpression: /-?\d*/
        }
        color: row.invalid ? Theme.error : Theme.text
        Accessible.name: row.modelData.name
        Component.onCompleted: text = String(row.current)
        onTextEdited: row.invalid = !row.settings.changeText(row.modelData, text)
        onEditingFinished: {
            text = String(row.current);
            row.invalid = false;
        }
    }
    // Follows the draft when it changes elsewhere ("Default", "Discard", the server's answer).
    onCurrentChanged: {
        if (!field.activeFocus || Number(field.text) !== current)
            field.text = String(current);
    }

    Item {
        Layout.fillWidth: true
    }
    GameButton {
        kind: GameButton.Quiet
        text: qsTr("Default")
        enabled: row.current !== row.modelData.defaultValue
        Accessible.name: qsTr("Set %1 to its default").arg(row.modelData.name)
        onClicked: row.settings.change(row.modelData, row.modelData.defaultValue)
    }
}

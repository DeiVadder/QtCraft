pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// "More world options" of the new world (decision E-917): a preset, then the options of the base
// game and the enabled mods in their groups, sliders for numbers and switches for on and off. The
// values belong to the new world's form (GameController.worldOptions): they hold when the page goes
// back to the form and count when the world is created. "‹ Back" (and Esc, the pad's B) returns to
// the form, "Restore defaults" sets every option to its default, "Create and play" creates the world
// as the form's own button does.
ColumnLayout {
    id: page

    required property GameController game
    readonly property WorldOptionsController options: game.worldOptions

    signal openRequested
    signal backRequested
    signal createRequested

    // Shows the page (also the test hook "@<ms> open:worldOptionsPage", which opens the form first,
    // so that the options start at their defaults).
    function open() {
        if (page.options.groups.length === 0)
            page.options.reset();
        form.contentY = 0;
        openRequested();
        presetChoice.forceActiveFocus(Qt.TabFocusReason);
    }

    // A value with its unit as the slider shows it ("384 blocks", "65 %", "1.5×").
    function valueText(unit: int, value: real): string {
        switch (unit) {
        case WorldOptionsController.Unit.Blocks:
            return qsTr("%1 blocks").arg(Math.round(value));
        case WorldOptionsController.Unit.Percent:
            return qsTr("%1 %").arg(Math.round(value * 100));
        case WorldOptionsController.Unit.Factor:
            return qsTr("%1×").arg(value.toLocaleString(Qt.locale(), "f", Number.isInteger(value * 10) ? 1 : 2));
        default:
            return value.toLocaleString(Qt.locale(), "f", Number.isInteger(value) ? 0 : 2);
        }
    }

    // The entry of the preset list for the chosen values: the defaults first, then the presets; -1
    // for values of one's own.
    function presetIndex(): int {
        if (page.options.changedCount === 0)
            return 0;
        const presets = page.options.presets;
        for (let i = 0; i < presets.length; ++i) {
            if (presets[i].name === page.options.preset)
                return i + 1;
        }
        return -1;
    }

    spacing: 0

    Connections {
        target: page.options

        function onValuesChanged() {
            presetChoice.currentIndex = page.presetIndex();
        }
        function onOptionsChanged() {
            presetChoice.currentIndex = page.presetIndex();
        }
    }

    SettingsPage {
        id: form

        objectName: "worldOptionsForm"
        Layout.fillWidth: true
        Layout.fillHeight: true
        title: qsTr("More world options")

        SettingGroup {
            title: qsTr("World generation")

            SettingDropdown {
                id: presetChoice

                objectName: "worldPresetChoice"
                label: qsTr("Preset")
                help: currentIndex > 0 ? page.options.presets[currentIndex - 1].help
                      : currentIndex === 0 ? qsTr("Every option as the game sets it.")
                      : qsTr("Options of your own; choose a preset to start again from it.")
                displayText: currentIndex < 0 ? qsTr("Custom") : model[currentIndex].title
                textRole: "title"
                valueRole: "name"
                model: [{ name: "", title: qsTr("Default") }].concat(page.options.presets)
                onActivated: index => page.options.choosePreset(index > 0 ? page.options.presets[index - 1].name : "")
                Component.onCompleted: currentIndex = page.presetIndex()
            }
        }

        // The map follows every change below (decision E-921).
        MapPreviewGroup {
            flickable: form
            preview: page.options.preview
        }

        Repeater {
            model: page.options.groups

            delegate: SettingGroup {
                id: group

                required property var modelData

                title: modelData.title

                Repeater {
                    model: group.modelData.options

                    delegate: ColumnLayout {
                        id: option

                        required property var modelData
                        readonly property var value: page.options.values[modelData.name]

                        Layout.fillWidth: true
                        spacing: 0

                        SettingSlider {
                            objectName: "worldOption:" + option.modelData.name
                            visible: !option.modelData.isSwitch
                            label: option.modelData.label
                            help: option.modelData.help
                            from: option.modelData.from
                            to: option.modelData.to
                            stepSize: option.modelData.stepSize
                            snapMode: Slider.SnapAlways
                            value: option.modelData.isSwitch ? 0 : (option.value ?? option.modelData.from)
                            valueText: page.valueText(option.modelData.unit, value)
                            onMoved: page.options.setValue(option.modelData.name, value)
                        }
                        SettingSwitch {
                            objectName: "worldOption:" + option.modelData.name
                            visible: option.modelData.isSwitch
                            label: option.modelData.label
                            help: option.modelData.help
                            checked: option.modelData.isSwitch && option.value === true
                            onToggled: page.options.setValue(option.modelData.name, checked)
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: Ui.size.line
        color: Theme.separator
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Ui.space.l
        spacing: Ui.space.s

        GameButton {
            kind: GameButton.Quiet
            text: qsTr("‹ Back")
            Accessible.name: qsTr("Back to the new world")
            onClicked: page.backRequested()
        }
        GameButton {
            objectName: "worldOptionsDefaults"
            enabled: page.options.changedCount > 0
            text: qsTr("Restore defaults")
            onClicked: page.options.choosePreset("")
        }
        Item {
            Layout.fillWidth: true
        }
        GameButton {
            kind: GameButton.Primary
            large: true
            text: qsTr("Create and play")
            onClicked: page.createRequested()
        }
    }
}

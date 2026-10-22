import QtQuick
import QtQuick.Layouts
import QtCraft

// The values of one gamepad profile (decision E-610): look speed and invert, the sticks' dead zone
// and response curve, the triggers' dead zone and the point where they act, vibration. Read-only
// while the edited controller uses the shared settings.
ColumnLayout {
    id: values

    required property GamepadOptions options

    Layout.fillWidth: true
    spacing: Ui.space.xs
    enabled: options.editable

    SettingSlider {
        objectName: "padLookSpeed"
        label: qsTr("Look speed")
        from: 30
        to: 720
        stepSize: 10
        value: values.options.lookSpeed
        valueText: qsTr("%1°/s").arg(Math.round(value))
        onMoved: values.options.lookSpeed = value
    }

    SettingSwitch {
        label: qsTr("Invert looking up and down")
        checked: values.options.invertLook
        onToggled: values.options.invertLook = checked
    }

    SettingSlider {
        label: qsTr("Stick dead zone")
        from: 0
        to: 0.5
        stepSize: 0.01
        value: values.options.deadZone
        valueText: qsTr("%1 %").arg(Math.round(value * 100))
        onMoved: values.options.deadZone = value
    }

    // Above 1 the sticks are finer near the centre.
    SettingSlider {
        label: qsTr("Response curve")
        help: qsTr("Above 1 the sticks are finer near the centre")
        from: 0.5
        to: 3
        stepSize: 0.1
        value: values.options.curve
        valueText: value.toLocaleString(Qt.locale(), "f", 1)
        onMoved: values.options.curve = value
    }

    SettingSlider {
        label: qsTr("Trigger dead zone")
        from: 0
        to: 0.5
        stepSize: 0.01
        value: values.options.triggerDeadZone
        valueText: qsTr("%1 %").arg(Math.round(value * 100))
        onMoved: values.options.triggerDeadZone = value
    }

    SettingSlider {
        objectName: "padTriggerThreshold"
        label: qsTr("Trigger acts at")
        from: 0.1
        to: 0.9
        stepSize: 0.05
        value: values.options.triggerThreshold
        valueText: qsTr("%1 %").arg(Math.round(value * 100))
        onMoved: values.options.triggerThreshold = value
    }

    SettingSwitch {
        label: qsTr("Vibrate when hurt")
        help: qsTr("If the controller can")
        checked: values.options.vibration
        onToggled: values.options.vibration = checked
    }
}

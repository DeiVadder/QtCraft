import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The values of one gamepad profile (decision E-610): look speed and invert, the sticks' dead zone
// and response curve, the triggers' dead zone and the point where they act, vibration. Read-only
// while the edited controller uses the shared settings.
GridLayout {
    id: values

    required property GamepadOptions options

    columns: 3
    columnSpacing: 12
    rowSpacing: 10
    enabled: options.editable

    Label {
        text: qsTr("Look speed")
    }
    Slider {
        id: lookSpeed

        objectName: "padLookSpeed"
        Layout.fillWidth: true
        from: 30
        to: 720
        stepSize: 10
        value: values.options.lookSpeed
        onMoved: values.options.lookSpeed = value
    }
    Label {
        Layout.preferredWidth: 90
        text: qsTr("%1°/s").arg(Math.round(lookSpeed.value))
    }

    CheckBox {
        Layout.columnSpan: 3
        text: qsTr("Invert looking up and down")
        checked: values.options.invertLook
        onToggled: values.options.invertLook = checked
    }

    Label {
        text: qsTr("Stick dead zone")
    }
    Slider {
        id: deadZone

        Layout.fillWidth: true
        from: 0
        to: 0.5
        stepSize: 0.01
        value: values.options.deadZone
        onMoved: values.options.deadZone = value
    }
    Label {
        text: qsTr("%1 %").arg(Math.round(deadZone.value * 100))
    }

    // Above 1 the sticks are finer near the centre.
    Label {
        text: qsTr("Response curve")
    }
    Slider {
        id: curve

        Layout.fillWidth: true
        from: 0.5
        to: 3
        stepSize: 0.1
        value: values.options.curve
        onMoved: values.options.curve = value
    }
    Label {
        text: curve.value.toLocaleString(Qt.locale(), "f", 1)
    }

    Label {
        text: qsTr("Trigger dead zone")
    }
    Slider {
        id: triggerDeadZone

        Layout.fillWidth: true
        from: 0
        to: 0.5
        stepSize: 0.01
        value: values.options.triggerDeadZone
        onMoved: values.options.triggerDeadZone = value
    }
    Label {
        text: qsTr("%1 %").arg(Math.round(triggerDeadZone.value * 100))
    }

    Label {
        text: qsTr("Trigger acts at")
    }
    Slider {
        id: triggerThreshold

        objectName: "padTriggerThreshold"
        Layout.fillWidth: true
        from: 0.1
        to: 0.9
        stepSize: 0.05
        value: values.options.triggerThreshold
        onMoved: values.options.triggerThreshold = value
    }
    Label {
        text: qsTr("%1 %").arg(Math.round(triggerThreshold.value * 100))
    }

    CheckBox {
        Layout.columnSpan: 3
        text: qsTr("Vibrate when hurt (if the controller can)")
        checked: values.options.vibration
        onToggled: values.options.vibration = checked
    }
}

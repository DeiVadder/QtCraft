import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// A setting with a slider (decision E-845, docs/UI-Leitfaden.md 6.4): the filled part in the
// accent, the empty track grey, a blocky knob, and the value with its unit in a fixed width on the
// right ("12 chunks", "70°", "80 %"). A slider with a `defaultValue` marks it on the track with a
// small notch (decision E-930), so the default can be found again. Left and right on the pad and
// the arrow keys move it. `moved()` follows a change by the player, as Slider's own.
SettingRow {
    id: setting

    property alias from: slider.from
    property alias to: slider.to
    property alias stepSize: slider.stepSize
    property alias snapMode: slider.snapMode
    property alias value: slider.value
    property string valueText
    // The default, marked on the track; NaN (the default) for no mark. A default outside from..to
    // has no mark either.
    property real defaultValue: NaN
    // The default as the value shows it ("384 blocks"), for screen readers.
    property string defaultText
    readonly property bool hasDefault: !isNaN(defaultValue) && slider.to > slider.from && defaultValue >= slider.from && defaultValue <= slider.to
    // Where the mark sits along the track, 0 to 1, mirrored as the slider is (right to left).
    readonly property real defaultPosition: {
        const position = hasDefault ? (defaultValue - slider.from) / (slider.to - slider.from) : 0;
        return slider.mirrored ? 1 - position : position;
    }

    signal moved

    Slider {
        id: slider

        Layout.fillWidth: true
        Accessible.name: setting.label
        Accessible.description: setting.hasDefault && setting.defaultText.length > 0 ? qsTr("Default: %1").arg(setting.defaultText) : ""
        implicitHeight: Ui.size.field
        padding: 0
        leftPadding: Ui.size.knobWidth / 2
        rightPadding: Ui.size.knobWidth / 2
        onMoved: setting.moved()

        background: Rectangle {
            x: slider.leftPadding
            y: (slider.height - height) / 2
            width: slider.availableWidth
            height: Ui.size.track
            radius: Ui.radius
            color: Theme.track

            Rectangle {
                width: slider.visualPosition * parent.width
                height: parent.height
                radius: Ui.radius
                color: Theme.accent
            }

            // The default's notch, above and below the track; taller than the knob, so it still
            // shows when the knob stands on it.
            Rectangle {
                objectName: "sliderDefaultMark"
                visible: setting.hasDefault
                x: setting.defaultPosition * parent.width - width / 2
                anchors.verticalCenter: parent.verticalCenter
                width: Ui.size.bar
                height: Ui.size.knobHeight + 2 * Ui.space.xs
                color: Theme.textMuted
            }
        }

        // The knob with a dark outline, which stands out on the accent fill and on the track alike.
        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * slider.availableWidth - width / 2
            y: (slider.height - height) / 2
            implicitWidth: Ui.size.knobWidth
            implicitHeight: Ui.size.knobHeight
            color: Theme.knobOutline

            Rectangle {
                anchors.fill: parent
                anchors.margins: Ui.size.line
                color: Theme.knob

                Bevel {
                    sunken: slider.pressed
                }
            }
            FocusOutline {
                visible: slider.visualFocus
            }
        }
    }

    Label {
        Layout.preferredWidth: Ui.size.value
        text: setting.valueText
        font.pixelSize: Ui.type.body
        color: Theme.text
        horizontalAlignment: Text.AlignRight
        elide: Text.ElideRight
    }
}

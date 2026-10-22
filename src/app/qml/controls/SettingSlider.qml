import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// A setting with a slider (decision E-845, docs/UI-Leitfaden.md 6.4): the filled part in the
// accent, the empty track grey, a blocky knob, and the value with its unit in a fixed width on the
// right ("12 chunks", "70°", "80 %"). Left and right on the pad and the arrow keys move it.
// `moved()` follows a change by the player, as Slider's own.
SettingRow {
    id: setting

    property alias from: slider.from
    property alias to: slider.to
    property alias stepSize: slider.stepSize
    property alias snapMode: slider.snapMode
    property alias value: slider.value
    property string valueText

    signal moved

    Slider {
        id: slider

        Layout.fillWidth: true
        Accessible.name: setting.label
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

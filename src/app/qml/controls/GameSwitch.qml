import QtQuick
import QtQuick.Controls
import QtCraft

// The game's on/off switch (decision E-845): a blocky knob on a sunken track, the track in the
// accent while on. Settings show it through SettingSwitch; lists (texture packs, mods) use it in
// their rows. Needs an Accessible.name from its user.
Switch {
    id: toggle

    // A change by the player: toggled() follows it as after a click.
    function flip() {
        toggle.toggle();
        toggle.toggled();
    }

    // The switch is its indicator alone (no text beside it).
    implicitWidth: Ui.size.switchWidth + leftPadding + rightPadding
    implicitHeight: Ui.size.switchHeight + topPadding + bottomPadding
    padding: 0
    spacing: 0
    // Enter switches it as Space does (docs/UI-Leitfaden.md 6.3).
    Keys.onReturnPressed: flip()
    Keys.onEnterPressed: flip()

    indicator: Rectangle {
        x: toggle.leftPadding
        y: (toggle.height - height) / 2
        implicitWidth: Ui.size.switchWidth
        implicitHeight: Ui.size.switchHeight
        radius: Ui.radius
        color: toggle.checked ? Theme.accent : Theme.track
        // Dimmed when only the switch is off; a disabled setting row dims it already.
        opacity: toggle.enabled || ((toggle.parent as ControlSlot)?.dimsControls ?? false) ? 1 : Ui.disabledOpacity

        Bevel {
            sunken: true
        }
        // The knob with a dark outline, which stands out on the accent and on the track alike.
        Rectangle {
            readonly property real inset: (parent.height - height) / 2

            x: toggle.checked ? parent.width - width - inset : inset
            y: inset
            width: Ui.size.switchKnob
            height: Ui.size.switchKnob
            color: Theme.knobOutline

            Rectangle {
                anchors.fill: parent
                anchors.margins: Ui.size.line
                color: Theme.knob

                Bevel {}
            }
        }
        FocusOutline {
            visible: toggle.visualFocus
        }
    }
    contentItem: Item {}
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The button of every game screen (decision E-845, docs/UI-Leitfaden.md 6.1). Primary (Qt green,
// dark text; at most one per screen, the likeliest next step, the pad's focus starts there),
// Secondary, Destructive (only for what cannot be undone or asks again) and Quiet (text only, "‹
// Back"). The blocky bevel marks its edge; pressed, the bevel turns inwards. A key or pad button
// can sit as a plate on the right (keyHint) instead of in brackets in the text.
Button {
    id: control

    enum Kind {
        Primary,
        Secondary,
        Destructive,
        Quiet
    }

    property int kind: GameButton.Secondary
    // Footer and pause menu buttons are taller.
    property bool large: false
    // The key shown as a plate at the right ("H"); empty for none.
    property string keyHint: ""

    readonly property color surface: {
        switch (kind) {
        case GameButton.Primary:
            return down ? Theme.accentPressed : hovered ? Theme.accentHover : Theme.accent;
        case GameButton.Destructive:
            return down || hovered ? Theme.dangerHover : Theme.danger;
        case GameButton.Quiet:
            return hovered ? Theme.rowHighlight : Theme.clear;
        default:
            return down ? Theme.buttonPressed : hovered ? Theme.buttonHover : Theme.button;
        }
    }

    highlighted: kind === GameButton.Primary
    implicitHeight: large ? Ui.size.buttonLarge : Ui.size.button
    leftPadding: kind === GameButton.Quiet ? Ui.space.s : Ui.space.l
    rightPadding: leftPadding
    topPadding: 0
    bottomPadding: 0
    font.pixelSize: Ui.type.body
    font.bold: kind === GameButton.Primary
    // Dimmed when only the button is off; a disabled setting row dims it already.
    opacity: enabled || ((parent as ControlSlot)?.dimsControls ?? false) ? 1 : Ui.disabledOpacity

    contentItem: RowLayout {
        spacing: Ui.space.s

        Label {
            Layout.fillWidth: true
            text: control.text
            font: control.font
            color: control.kind === GameButton.Primary ? Theme.accentText
                   : control.kind === GameButton.Quiet ? Theme.textSecondary : Theme.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        KeyBadge {
            visible: control.keyHint.length > 0
            text: control.keyHint
        }
    }

    background: Rectangle {
        implicitWidth: Ui.size.button
        implicitHeight: control.implicitHeight
        radius: Ui.radius
        color: control.surface

        Bevel {
            visible: control.kind !== GameButton.Quiet
            sunken: control.down || control.checked
        }
        FocusOutline {
            visible: control.visualFocus
        }
    }
}

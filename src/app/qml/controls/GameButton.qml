import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The button of every game screen (decision E-845, docs/UI-Leitfaden.md 6.1). Primary (Qt green,
// dark text; at most one per screen, the likeliest next step, the pad's focus starts there),
// Secondary, Destructive (only for what cannot be undone or asks again) and Quiet (text only, "‹
// Back"). The blocky bevel marks its edge; pressed, the bevel turns inwards. A key or pad button
// can sit as a plate on the right (keyHint) instead of in brackets in the text. Enter clicks it as
// Space does. A disabled button of any kind is flat and grey (buttonDisabled, textDisabled), so a
// disabled primary button does not read as the accent; in a dimmed SettingRow it keeps its colours
// and the row's dimming alone shows it (no grey on grey).
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

    // Disabled and not already dimmed by its SettingRow (ControlSlot.dimsControls).
    readonly property bool greyed: !enabled && !((parent as ControlSlot)?.dimsControls ?? false)
    readonly property color surface: {
        if (greyed)
            return kind === GameButton.Quiet ? Theme.clear : Theme.buttonDisabled;

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
    // Enter clicks a focused button as Space does (A on the pad clicks it too).
    Keys.onReturnPressed: click()
    Keys.onEnterPressed: click()

    contentItem: RowLayout {
        spacing: Ui.space.s

        Label {
            Layout.fillWidth: true
            text: control.text
            font: control.font
            color: control.greyed ? Theme.textDisabled
                   : control.kind === GameButton.Primary ? Theme.accentText
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
            // A disabled button is flat: no edge invites a click.
            visible: control.kind !== GameButton.Quiet && !control.greyed
            sunken: control.down || control.checked
        }
        FocusOutline {
            visible: control.visualFocus
        }
    }
}

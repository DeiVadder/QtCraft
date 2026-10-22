import QtQuick
import QtQuick.Controls
import QtCraft

// A dialog that needs an answer (decision E-852, docs/UI-Leitfaden.md 6.0): modal for the whole
// window. It is centred over the window, not over the page that opened it, and the strong scrim
// lies over everything below it, the side bar included; nothing below takes a click, a key or the
// pad's focus until it closes. UnsavedChangesDialog and DeleteWorldDialog are such dialogs.
Dialog {
    // Centred in the window's overlay, wherever the dialog is declared (a page beside the side bar).
    anchors.centerIn: Overlay.overlay
    // A fixed width: the style's implicit width would follow the scaled font in a loop.
    width: Math.min((Overlay.overlay ? Overlay.overlay.width : parent.width) - 2 * Ui.screen.margin, Ui.screen.s)
    modal: true
    dim: true
    padding: Ui.space.xl
    background: ScreenFrame {}

    Overlay.modal: Rectangle {
        color: Theme.scrimStrong
    }
}

import QtQuick
import QtCraft

// The surface of every screen and dialog (decision E-845, docs/UI-Leitfaden.md 5): the opaque
// panel with its light edge, nearly square, so the screen below (pause menu, main menu) does not
// show through. Popups take it as their background instead of the style's, whose edge is the
// accent.
Rectangle {
    color: Theme.panelSolid
    border.color: Theme.panelBorder
    border.width: Ui.size.line
    radius: Ui.radius
}

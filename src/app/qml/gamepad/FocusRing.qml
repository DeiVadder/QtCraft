import QtQuick
import QtCraft

// The ring around the item the pad's focus is on (decision E-583), over every screen; inside an
// open popup it moves into the popup (GameController.gamepad.focusLayer). Shown only while the
// pad leads on a screen.
Rectangle {
    id: ring

    required property GameController game
    // Where it lies while the focus is on a screen.
    required property Item home
    readonly property point origin: parent ? parent.mapFromItem(null, game.gamepad.focusRect.x, game.gamepad.focusRect.y) : Qt.point(0, 0)

    parent: game.gamepad.focusLayer ?? home
    z: 1000
    x: origin.x - Ui.px(3)
    y: origin.y - Ui.px(3)
    width: game.gamepad.focusRect.width + 2 * Ui.px(3)
    height: game.gamepad.focusRect.height + 2 * Ui.px(3)
    // The rect is clipped to the lists around the focus: empty while it is scrolled away.
    visible: game.gamepad.focusShown && game.gamepad.focusRect.width > 0 && game.gamepad.focusRect.height > 0
    color: Theme.clear
    border.color: Theme.focusRing
    border.width: Ui.px(3)
    radius: Ui.px(4)
}

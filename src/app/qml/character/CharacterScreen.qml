import QtQuick
import QtQuick.Controls
import QtCraft

// The character screen over the game (decision E-231): the CharacterEditor on the screen's panel,
// editing a draft that applies with "Save" (decision E-852). Esc, the pad's B and "Close" ask first
// when there are unsaved changes. The main menu shows the same editor as a page (CharacterPage).
Popup {
    id: screen

    required property GameController game

    x: Ui.screen.margin
    y: (parent.height - height) / 2
    width: Math.min(parent.width - 2 * Ui.screen.margin, Ui.screen.l)
    modal: true
    background: ScreenFrame {}
    focus: true
    padding: Ui.space.xl
    // Esc goes through the editor, which asks about unsaved changes.
    closePolicy: Popup.NoAutoClose

    onAboutToShow: editor.begin()
    onClosed: editor.end()

    CharacterEditor {
        id: editor

        anchors.fill: parent
        game: screen.game
        showClose: true
        onCloseRequested: screen.close()
        Keys.onEscapePressed: editor.leave(() => screen.close())
    }
}

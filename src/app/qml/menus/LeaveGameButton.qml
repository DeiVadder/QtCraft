import QtQuick
import QtCraft

// The way out of a world, the same in the pause menu and on the death screen (Menue-Pruefung D.1,
// TD-1): "Save and quit to menu" for a saved world, "Quit to menu" for one that is not kept,
// "Disconnect" in a network game. Secondary and as wide as the screen's other buttons.
GameButton {
    required property GameController game

    objectName: "leaveGameButton"
    large: true
    text: game.multiplayer.networkGame ? qsTr("Disconnect") : game.persistent ? qsTr("Save and quit to menu") : qsTr("Quit to menu")
    onClicked: game.leaveGame()
}

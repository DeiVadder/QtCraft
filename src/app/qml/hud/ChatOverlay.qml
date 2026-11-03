// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtCraft

// The chat line (decision E-094): a chat line or a /command; while the line is empty or a command
// name is being typed, a hint above it points to /help (decision E-911).
Item {
    id: overlay

    required property GameController game
    // Where the HUD shows the hotbar and the bars above it, in this item's frame: the hint stays left
    // of it, or above it when there is too little room on the left (narrow window, large UI size).
    property rect statusArea: Qt.rect(width, height, 0, 0)
    // The last line sent was a command: whoever just ran one needs no pointer to /help on the empty
    // line (the hint comes back once a command name is typed).
    property bool commandSent: false

    onVisibleChanged: {
        if (visible) {
            input.text = overlay.game.chatStart;
            input.forceActiveFocus();
            // Opened with the pad: the line is typed on the on-screen keyboard (decision E-584).
            overlay.game.gamepad.requestKeyboard(input);
        }
    }
    // Keys the line leaves (function keys, ...) end here.
    Keys.onPressed: event => event.accepted = true

    HudLabel {
        id: helpHint

        // The room left of the status area, beside the chat line's left edge.
        readonly property real roomLeft: overlay.statusArea.x - input.x - Ui.hudPx(8)
        readonly property bool besideHotbar: roomLeft >= Ui.hudPx(160)

        x: input.x
        y: (besideHotbar ? input.y : Math.min(input.y, overlay.statusArea.y)) - height - Ui.hudPx(4)
        width: Math.min(implicitWidth, besideHotbar ? roomLeft : input.width)
        wrapMode: Text.Wrap
        visible: overlay.game.hud.chat.showsHelpHint(input.text, overlay.commandSent)
        text: qsTr("/help lists the commands by group, /help <command> explains one")
    }

    TextField {
        id: input

        // The startup hooks' text: step and QTCRAFT_LOG_ITEMS find it (app_offscreen_chat_history).
        objectName: "chatInput"
        Accessible.name: qsTr("Chat")
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Ui.hudPx(12)
        placeholderText: overlay.game.gamepad.active ? qsTr("Chat or /command (e.g. /help), %1 on the keyboard sends, %2 cancels").arg(overlay.game.gamepad.pads.texts.start).arg(overlay.game.gamepad.pads.texts.east)
                                                   : qsTr("Chat or /command (e.g. /help), Enter to send, Esc to cancel")
        // The lines sent before, as in a shell (decision E-970); past the newest the draft.
        Keys.onUpPressed: event => input.recall(event, true)
        Keys.onDownPressed: event => input.recall(event, false)
        Keys.onPressed: event => {
            event.accepted = overlay.game.settings.keys.screenKey(event.key, event.isAutoRepeat, KeyControls.ClosingKey.EscOnly) === KeyControls.ScreenKey.Close;
            if (event.accepted)
                overlay.game.overlay = GameController.Overlay.None;
        }
        // Up or down without modifiers (the arrow keys carry the keypad flag on macOS) recall a
        // line; with Shift, Ctrl or Cmd the field keeps them (selection, start and end).
        function recall(event: KeyEvent, back: bool) {
            event.accepted = (event.modifiers & ~Qt.KeypadModifier) === Qt.NoModifier;
            if (event.accepted)
                text = back ? overlay.game.hud.chat.inputs.previous(text) : overlay.game.hud.chat.inputs.next(text);
        }
        onAccepted: {
            overlay.commandSent = overlay.game.hud.chat.isCommand(text);
            overlay.game.sendChat(text);
            overlay.game.overlay = GameController.Overlay.None;
        }
    }
}

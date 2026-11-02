// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtCraft

// A screen of the window in Screens.qml (decision E-691): a LazyScreen that fills its parent,
// takes the game of the Screens it lies in, keeps the game's keep-alive time, logs its making as
// the game says and shows in `phase` while `open` holds. By default `open` follows `overlay`: the
// screen shows while that in-game screen is open.
LazyScreen {
    readonly property GameController game: (parent as Screens).game
    // Enumerations of C++ types are int properties in QML (a QML property cannot be declared with
    // a C++ enum type), and -1, which no enumerator has, stands for "any".
    // The phase the screen shows in (GameController.Phase), -1 in every phase.
    property int phase: GameController.Phase.Playing
    // The in-game screen this screen shows (GameController.Overlay), -1 for none: `open` decides.
    property int overlay: -1
    // Whether the screen is open within its phase.
    property bool open: overlay < 0 || game.overlay === overlay

    anchors.fill: parent
    keepAliveMs: game.screenKeepAliveMs
    logCreation: game.logScreenCreation
    shown: (phase < 0 || game.phase === phase) && open
}

// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// A screen that exists only while it is shown and for a while after (decision E-691): a Loader
// makes it when `shown` turns true and keeps it for `keepAliveMs` after it closed, so a screen
// opened again soon comes back at once; each close starts the time again, a reopen stops it. Then
// the screen is unloaded and gives its objects and memory back. State that must outlive the screen
// (tabs, search text, scroll positions that matter) lives in the C++ controllers.
// The screen is made hidden and shown once it is complete, so its onVisibleChanged handlers (the
// focus a screen takes when it opens) run on the first opening as on every later one. Loading is
// asynchronous (decision E-875): the screen is made over the next frames instead of stalling the
// GUI thread (the first pause menu took long enough for the sound to drop out), and it shows,
// takes the focus and reacts to the pad once it is complete (`loaded`). `asynchronous: false`
// makes it within the change of `shown`.
Item {
    id: lazy

    // Whether the screen is to be seen now.
    property bool shown: false
    // Made already while true, hidden until shown, and kept as long: for a screen that takes longer
    // than a frame to make and is opened often (the creative inventory, decision E-691).
    property bool preload: false
    // How long a closed screen stays loaded; 0 unloads it at once.
    property int keepAliveMs: 60000
    // Made over the next frames (see above).
    property bool asynchronous: true
    // The screen: a Component whose item fills this one.
    property Component screen
    // The loaded screen, null while unloaded.
    readonly property Item item: loader.item as Item
    readonly property bool loaded: loader.status === Loader.Ready
    // To be seen but still being made: the screenshot hook waits while any screen is pending
    // (QcScreenshotHook, decision E-875), so pictures always show the complete screen.
    readonly property bool screenPending: shown && !loaded && screen !== null
    // Logs "UI stats: <screen> made in <n> ms" each time the screen is made (decision E-690).
    property bool logCreation: false
    // When the Loader was told to make the screen (Date.now()), for logCreation.
    property double makeStartMs: 0
    // The screen is closed but still loaded, waiting for the keep-alive time to run out (also while
    // preload keeps it).
    readonly property bool keepingAlive: keepTimer.running
    // True while the screen is loaded: shown, or closed less than keepAliveMs ago.
    property bool live: shown

    visible: shown

    // Unloads the closed screen now, as when its keep-alive time runs out (tests advance the clock
    // through this; a shown screen stays).
    // Passes `make` through to Loader.active and notes the time when it is true: the binding runs
    // only when `live` changes, before the Loader starts to make the screen and sends `loaded`.
    function startClock(make: bool): bool {
        if (make)
            makeStartMs = Date.now();
        return make;
    }

    function expireKeepAlive() {
        keepTimer.stop();
        live = shown;
    }

    onShownChanged: {
        if (shown) {
            keepTimer.stop();
            live = true;
        } else if (keepAliveMs > 0) {
            keepTimer.restart();
        } else {
            live = false;
        }
    }

    Timer {
        id: keepTimer

        interval: lazy.keepAliveMs
        onTriggered: lazy.expireKeepAlive()
    }

    // Hidden until the screen is complete (see above).
    Item {
        anchors.fill: parent
        visible: lazy.loaded

        Loader {
            id: loader

            anchors.fill: parent
            asynchronous: lazy.asynchronous
            active: lazy.startClock(lazy.live || lazy.preload)
            sourceComponent: lazy.screen

            onLoaded: {
                if (lazy.logCreation)
                    console.info("UI stats: " + String(item).split("(")[0] + " made in " + (Date.now() - lazy.makeStartMs) + " ms");
            }
        }
    }
}

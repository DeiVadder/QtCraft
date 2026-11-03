// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// Keeps something alive for a while after it was last shown (decision E-691): `live` is true while
// `shown` and for `interval` ms after it turned false; each hiding starts the time again, showing
// again stops it, 0 ends it at once. LazyScreen keeps its screens this way and
// CharacterPreviewHost its preview. While the time runs, `running` is true.
Timer {
    id: keep

    // Whether the thing is to be seen now.
    property bool shown: false
    // True while shown, and for `interval` ms after.
    property bool live: shown

    // Ends the time now, as when it runs out (a shown thing stays live).
    function expire() {
        stop();
        live = shown;
    }

    interval: 60000
    onTriggered: expire()
    onShownChanged: {
        if (shown) {
            stop();
            live = true;
        } else if (interval > 0) {
            restart();
        } else {
            live = false;
        }
    }
}

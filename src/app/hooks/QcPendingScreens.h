// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

class QQuickWindow;

// Screens that are to be seen but still being made (LazyScreen.screenPending, decision E-875):
// screenshots and the scripted pad's reports wait for them, so they always see the complete
// screen. A LazyScreen loads asynchronously over the frames after it is shown.
class QcPendingScreens
{
public:
    // After the frames a hook waits anyway, it waits at most this many further frames for
    // pending screens (a screen that fails to load must not hold it for ever).
    static constexpr int maxWaitFrames = 600;

    // Whether a shown LazyScreen in `window` is still being made. The items keep their QObject
    // parents when offscreen runs move them into another window.
    [[nodiscard]] static bool any(const QQuickWindow &window);
};

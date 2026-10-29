// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/preview/QcPreviewActivity.h"

namespace QcPreviewActivity {

Mode mode(State state)
{
    const bool windowShown = state.window == Window::Inactive || state.window == Window::Active;
    if (!state.visible || !state.hasSize || !state.hasSource || !windowShown) {
        return Mode::Off;
    }
    const bool inactive = state.window == Window::Inactive && !state.forceActive;
    return state.paused || inactive ? Mode::Still : Mode::Animated;
}

bool timerRuns(Mode mode, bool figureDrawn)
{
    return mode == Mode::Animated || (mode == Mode::Still && !figureDrawn);
}

} // namespace QcPreviewActivity

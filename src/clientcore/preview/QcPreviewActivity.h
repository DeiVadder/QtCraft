// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/*! \brief When a character preview draws (decision E-231).

    The item reports what it knows about itself and its window; the rules are GUI-free, so they
    are testable. */
namespace QcPreviewActivity {

/*! \brief The state of the window the preview lies in. */
enum class Window {
    /*! Not in a window yet. */
    None,
    /*! The window is hidden. */
    Hidden,
    /*! The window is minimised. */
    Minimised,
    /*! Shown, but another program has the focus (or a scripted or offscreen run). */
    Inactive,
    /*! Shown and focused. */
    Active,
};

/*! \brief What the preview item knows about itself and its window. */
struct State
{
    /*! Effectively visible: the item and all its parents. */
    bool visible = false;
    /*! Has a size above zero. */
    bool hasSize = false;
    /*! Something to draw from (QcPreviewSource). */
    bool hasSource = false;
    /*! State of the window. */
    Window window = Window::None;
    /*! Covered by another screen (the main menu's card while the character screen is open). */
    bool paused = false;
    /*! Benchmark hook QTCRAFT_PREVIEW_ACTIVE=1: an inactive window (scripted, background or
        offscreen run) counts as active, so the preview animates as in a played game and its
        costs can be measured. Paused and off still win. */
    bool forceActive = false;
};

/*! \brief How the preview draws. */
enum class Mode {
    /*! Nothing is drawn and no timer runs. */
    Off,
    /*! The figure as it is: drawn until a frame shows it, then the picture stays. */
    Still,
    /*! Animated at the preview's frame rate. */
    Animated,
};

/*! Returns how a preview in \a state draws: off unless it is visible with a size and a source in a
    shown window; still while paused or in an inactive window (unless forceActive); animated
    otherwise. */
Mode mode(State state);

/*! Returns whether the frame timer runs in \a mode: always while animated; while still only until a
    frame showed the figure (\a figureDrawn) (the content may still be loading). */
bool timerRuns(Mode mode, bool figureDrawn);

} // namespace QcPreviewActivity

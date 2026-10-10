#pragma once

// When a character preview draws (decision E-231). The item reports what it knows about itself and
// its window; the rules are GUI-free, so they are testable.
namespace QcPreviewActivity {

enum class Window {
    // Not in a window yet.
    None,
    Hidden,
    Minimised,
    // Shown, but another program has the focus (or a scripted or offscreen run).
    Inactive,
    Active,
};

struct State
{
    // Effectively visible: the item and all its parents.
    bool visible = false;
    bool hasSize = false;
    // Something to draw from (QcPreviewSource).
    bool hasSource = false;
    Window window = Window::None;
    // Covered by another screen (the main menu's card while the character screen is open).
    bool paused = false;
    // Benchmark hook QTCRAFT_PREVIEW_ACTIVE=1: an inactive window (scripted, background or offscreen
    // run) counts as active, so the preview animates as in a played game and its costs can be
    // measured. Paused and off still win.
    bool forceActive = false;
};

enum class Mode {
    // Nothing is drawn and no timer runs.
    Off,
    // The figure as it is: drawn until a frame shows it, then the picture stays.
    Still,
    // Animated at the preview's frame rate.
    Animated,
};

Mode mode(State state);

// Whether the frame timer runs: always while animated; while still only until a frame showed the
// figure (the content may still be loading).
bool timerRuns(Mode mode, bool figureDrawn);

} // namespace QcPreviewActivity

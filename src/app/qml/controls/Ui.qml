pragma Singleton

import QtQuick
import QtCraft

// The sizes of the UI at the player's scale (decision E-725): every size of a screen goes through
// px(), every size of the HUD through hudPx(), so one factor (UiScaling) scales fonts, slots,
// margins and panels alike. Sizes are written as at 100 % in a 1024 x 640 window; the factors are
// already limited so that a screen still fits into the window. Bindings that call px() follow the
// factor (the functions read it as a property).
QtObject {
    // Screens, menus and their popups.
    readonly property real scale: UiScaling.scale
    // The HUD over the world (hotbar, status rows, chat lines, notices).
    readonly property real hudScale: UiScaling.hudScale

    // A size at `factor`, in whole pixels (borders and gaps stay sharp); for items that are on
    // screens and in the HUD alike (ItemSlot.unit).
    function scaled(size: real, factor: real): real {
        return Math.round(size * factor);
    }

    // A screen size at the current scale.
    function px(size: real): real {
        return scaled(size, scale);
    }

    // A HUD size at the current HUD scale.
    function hudPx(size: real): real {
        return scaled(size, hudScale);
    }
}

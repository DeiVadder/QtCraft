pragma Singleton

import QtQuick
import QtCraft

// The sizes of the UI at the player's scale (decision E-725): every size of a screen goes through
// px(), every size of the HUD through hudPx(), so one factor (UiScaling) scales fonts, slots,
// margins and panels alike. Sizes are written as at 100 % in a 1024 x 640 window; the factors are
// already limited so that a screen still fits into the window. Bindings that call px() follow the
// factor (the functions read it as a property).
//
// The named steps below (decision E-845, docs/UI-Leitfaden.md) are the one place where the menus'
// spacing, sizes, screen widths, radius and type scale are set: Ui.space.m, Ui.size.row,
// Ui.screen.l, Ui.radius, Ui.type.title and so on, already at the player's scale. Changing a value
// here changes every screen built from them; the colours are in QcUiPalette (Theme). The settings
// screen and the controls of this folder use no sizes of their own (test_ui_palette).
QtObject {
    id: ui

    // Gaps, from the smallest: label and help, controls in a row, setting rows, card padding and
    // footer buttons, groups and screen padding, the main menu's margin to the window.
    component Spacing: QtObject {
        property real xs
        property real s
        property real m
        property real l
        property real xl
        property real xxl
    }

    // Sizes of the controls.
    component Sizes: QtObject {
        // A setting row (the focus target of mouse and pad), a button, a footer or pause button, a
        // field (drop-down, segment).
        property real row
        property real button
        property real buttonLarge
        property real field
        // Thin lines: borders and separators; the bevel's light and dark edges; the focus outline.
        property real line
        property real bevel
        property real bevelDeep
        property real focus
        // The coloured bar left of a chosen category and of a hint.
        property real bar
        // The width of a slider's value ("12 Chunks", "80 %").
        property real value
        // One entry's height in a side bar (main menu, settings, handbook).
        property real navItem
        // A small square symbol (category, list row).
        property real icon
        // Slider: track height, knob width and height. Switch: width, height, knob.
        property real track
        property real knobWidth
        property real knobHeight
        property real switchWidth
        property real switchHeight
        property real switchKnob
        // A key or pad button shown as a small plate.
        property real badge
        // The key buttons of the key table and the "Change" buttons of the pad's buttons.
        property real keyButton
        property real padButton
        // A texture pack's preview.
        property real previewWidth
        property real previewHeight
        // The lowest height of a list that scrolls on its own.
        property real listMinimum
        // The main menu's side bar (logo, entries, profile, quit).
        property real menuNav
        // A world's tile in the main menu's "continue" card and in a row of the world list.
        property real thumbnail
        property real thumbnailSmall
        // The small figure of the player's profile in the main menu's side bar.
        property real figureWidth
        property real figureHeight
        // The character screen's preview, its lowest height, and its colour swatches.
        property real preview
        property real previewMinimum
        property real swatch
        // The label column of a form whose labels stand left of their fields (character screen).
        property real label
        // The column of a form beside a list (the multiplayer screen's address, name and password).
        property real formColumn
        // The handbook's list of entries beside the open page.
        property real handbookEntries
        // A row of that list (an entry, a heading) and an entry's item icon.
        property real handbookRow
        property real handbookHeading
        property real handbookIcon
        // The scroll bar (ThemedScrollBar) and its handle.
        property real scrollBar
        property real scrollHandle
        // The room a screen leaves below itself for the pad's hints (PadHints) while they show.
        property real padHints
        // A small line chart (LineChart) and the list of the admin screen's chat, log and output.
        property real chart
        property real lines
        // The widest a tooltip's text runs before it wraps (ThemedToolTip).
        property real tooltip
        // The square of the map preview of a new world (MapPreviewGroup).
        property real map
        // The workbench screens (decision E-884): screen pixels per pixel of their pixel art (runes,
        // book, symbols; whole pixels keep it blocky) and an enchanting offer's field.
        property real artPixel
        property real offerWidth
        property real offerHeight
    }

    // Widths of screens: dialogs, forms and lists, settings with their category list; the margin
    // a screen keeps to the window's edges at the sides and at the top and bottom.
    component Screens: QtObject {
        property real s
        property real m
        property real l
        property real margin
        property real marginVertical
        // A single column of buttons (pause menu).
        property real column
        // The height of a screen with a list (mods, multiplayer), at most.
        property real height
    }

    // Font sizes: logo (and in low windows), screen title, section, group heading, body text, help.
    component TypeScale: QtObject {
        property real logo
        property real logoSmall
        property real title
        property real section
        property real group
        property real body
        property real help
        property real badge
    }

    // Screens, menus and their popups.
    readonly property real scale: UiScaling.scale
    // The HUD over the world (hotbar, status rows, chat lines, notices).
    readonly property real hudScale: UiScaling.hudScale

    readonly property Spacing space: Spacing {
        xs: ui.px(4)
        s: ui.px(8)
        m: ui.px(12)
        l: ui.px(16)
        xl: ui.px(24)
        xxl: ui.px(32)
    }

    readonly property Sizes size: Sizes {
        row: ui.px(40)
        button: ui.px(32)
        buttonLarge: ui.px(40)
        field: ui.px(30)
        line: Math.max(1, ui.px(1))
        bevel: Math.max(1, ui.px(1))
        bevelDeep: Math.max(1, ui.px(2))
        focus: Math.max(1, ui.px(2))
        bar: ui.px(3)
        value: ui.px(72)
        navItem: ui.px(38)
        icon: ui.px(18)
        track: ui.px(6)
        knobWidth: ui.px(12)
        knobHeight: ui.px(18)
        switchWidth: ui.px(42)
        switchHeight: ui.px(22)
        switchKnob: ui.px(16)
        badge: ui.px(18)
        keyButton: ui.px(130)
        padButton: ui.px(170)
        previewWidth: ui.px(96)
        previewHeight: ui.px(48)
        listMinimum: ui.px(160)
        menuNav: ui.px(224)
        thumbnail: ui.px(88)
        thumbnailSmall: ui.px(36)
        figureWidth: ui.px(36)
        figureHeight: ui.px(52)
        preview: ui.px(300)
        previewMinimum: ui.px(460)
        swatch: ui.px(28)
        label: ui.px(110)
        formColumn: ui.px(300)
        padHints: ui.px(48)
        chart: ui.px(64)
        lines: ui.px(200)
        tooltip: ui.px(360)
        map: ui.px(280)
        artPixel: Math.max(1, ui.px(2))
        offerWidth: ui.px(320)
        offerHeight: ui.px(40)
        handbookEntries: ui.px(220)
        handbookRow: ui.px(36)
        handbookHeading: ui.px(30)
        handbookIcon: ui.px(24)
        scrollBar: ui.px(10)
        scrollHandle: ui.px(6)
    }

    readonly property Screens screen: Screens {
        s: ui.px(480)
        m: ui.px(760)
        l: ui.px(960)
        margin: ui.px(32)
        marginVertical: ui.px(16)
        column: ui.px(320)
        height: ui.px(560)
    }

    // The pixel font Pixelify Sans (SIL Open Font License 1.1, src/app/fonts) for the logo and the
    // workbench screens' titles, runes and levels (decision E-884).
    readonly property FontLoader pixelFont: FontLoader {
        source: "qrc:/qt/qml/QtCraft/fonts/PixelifySans.ttf"
    }

    readonly property TypeScale type: TypeScale {
        logo: ui.px(64)
        logoSmall: ui.px(48)
        title: ui.px(24)
        section: ui.px(18)
        group: ui.px(14)
        body: ui.px(14)
        help: ui.px(12)
        badge: ui.px(11)
    }

    // Buttons, fields, cards and plates: blocky, nearly square corners.
    readonly property real radius: ui.px(2)
    // A control that cannot be used now (with the reason in its help text).
    readonly property real disabledOpacity: 0.45

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

    // A page beside a side bar `barWidth` wide at the window's left edge, in a window
    // `windowWidth` wide (the main menu's pages, SidebarScreen; decision E-875): it keeps the
    // menu's margin to the bar and the window and is no wider than a large screen.
    function pageWidth(barWidth: real, windowWidth: real): real {
        return Math.min(windowWidth - barWidth - 2 * screen.margin, screen.l);
    }

    // The left edge of such a page `width` wide: after the bar and the margin, and in the middle
    // of the room beside the bar when the window is wider than the page needs (a large monitor in
    // fullscreen).
    function pageX(barWidth: real, windowWidth: real, width: real): real {
        return barWidth + screen.margin + Math.max(0, (windowWidth - barWidth - 2 * screen.margin - width) / 2);
    }
}

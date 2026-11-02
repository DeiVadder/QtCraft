// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QList>
#include <QPalette>
#include <QString>
#include <QStringView>

#include <optional>
#include <span>

/*! \brief The colours of every game screen in one place.

    QML reads them as \c {Theme} (QcUiTheme); screens use no colours of their own. The text pairs
    below say which text colour stands on which backgrounds, so one test checks the contrast of
    all screens over the brightest and darkest world (snow, desert, water, night) instead of each
    screen on its own. */
namespace QcUiPalette {

/*! The colour roles; each is a property of \c {Theme} in QML. */
enum class Role {
    /*! Texts: the normal text. */
    Text,
    TextSecondary, // less important text
    TextMuted, // hints and disabled-looking text
    Heading, // headings
    Link, // links
    Warning, // warnings
    Error, // errors
    /*! Shadow and outline around texts and icons over the world. */
    TextOutline,
    /*! Backgrounds, from the world upwards: behind HUD texts on the world. */
    HudBackdrop,
    Scrim, // dims the world behind a screen
    ScrimStrong, // dims the world more
    DeathScrim, // the death screen over the world
    Panel, // the panels of the screens
    /*! The panel without transparency: screens and dialogs over other screens (ScreenFrame). */
    PanelSolid,
    PanelRaised, // a part raised on a panel (groups, the chosen row)
    PanelBorder, // the edge of a panel
    MenuTop, // top of the main menu's gradient
    MenuBottom, // bottom of the main menu's gradient
    /*! Slots, bars and markers: a slot on a panel. */
    Slot,
    /*! Hotbar slots lie directly on the world: darker than slots on a panel. */
    HudSlot,
    SlotHover, // a slot under the pointer
    SlotBorder, // the edge of a slot
    /*! Edge of the colour swatches of the character screen: visible on the panel and around the
        darkest colours (black hair), 3:1 as WCAG asks of controls. */
    SwatchBorder,
    SlotPicked, // a slot whose stack is picked up
    /*! Faint light behind item icons, so dark items (coal, obsidian) stand out on dark slots. */
    IconBackdrop,
    Selection, // the edge of the selected slot
    /*! The ring around the item the pad's focus is on (decision E-583). */
    FocusRing,
    /*! Pad button symbols (decision E-612): the disc behind a symbol and the colours of the face
        buttons' letters and shapes (plain colours, no logos). */
    PadGlyph,
    PadGreen, // the green face button
    PadRed, // the red face button
    PadBlue, // the blue face button
    PadYellow, // the yellow face button
    PadPink, // the pink face button
    Crosshair, // the crosshair
    Progress, // progress bars
    Health, // the health bar
    /*! The experience bar and level in the HUD (decision E-398). */
    Experience,
    /*! The air bubbles in the HUD (decision E-955). */
    Air,
    Flame, // the flame of a burning furnace
    FlameCore, // the core of that flame
    DurabilityGood, // durability bar of a tool in good shape
    DurabilityLow, // durability bar of a worn tool
    DurabilityBad, // durability bar of a tool about to break
    /*! States shown as a graphic, e.g. the signal bars of the player list (decision E-886):
        good, warning, bad. Tools keep their own durability colours. */
    StatusGood,
    StatusWarn, // warning
    StatusBad, // bad
    InfoButton, // the info button of a slot
    InfoButtonHover, // the info button under the pointer
    /*! Handbook: separators, the selected row, link chips. */
    Separator,
    RowHighlight, // the selected row
    LinkBackground, // link chips
    LinkHover, // a link chip under the pointer
    /*! Tooltips: nearly opaque, so the text reads the same over panels, headings and the world. */
    TooltipBackground,
    /*! Debug display: the frame and tick graphs and their budget line (16.7 ms, 50 ms). */
    GraphFrame,
    GraphTick, // the tick graph
    GraphLimit, // the budget line
    /*! Dials of a held compass or clock (decision E-769): the compass face and rim, the clock's
        night and day halves, rim and hand. */
    DialFace,
    DialRim, // compass rim
    ClockNight, // night half of the clock
    ClockDay, // day half of the clock
    ClockRim, // clock rim
    ClockHand, // clock hand
    /*! The wooden board of the sign editor (decision E-766). */
    SignBoard,
    SignBorder, // the edge of the board
    /*! A filled map in the hand (decision E-805): the paper with its edge and the players on it,
        the own one and the others, and the item frames showing the map (decision E-808). */
    MapPaper,
    MapPaperEdge, // the edge of the paper
    MapMarker, // the own player
    MapMarkerOther, // the other players
    MapMarkerFrame, // item frames showing the map
    /*! Controls of the menus (decision E-845): the accent (Qt green) of the primary button, the
        filled part of sliders and switches and the chosen category, with the dark text that
        stands on it (white on Qt green reaches only 2.1:1); the secondary button in its states,
        the destructive button, the sunken field of text fields and lists, the empty track of
        sliders and switches (3:1 to the panel, WCAG 1.4.11), the knob of sliders and switches
        and the bevel that gives buttons their blocky edge (light above and left, dark below and
        right). */
    Accent,
    AccentHover, // the accent under the pointer
    AccentPressed, // the accent pressed
    AccentText, // text on the accent
    Button, // the secondary button
    ButtonHover, // the secondary button under the pointer
    ButtonPressed, // the secondary button pressed
    Danger, // the destructive button
    DangerHover, // the destructive button under the pointer
    /*! A button that cannot be used now, of any kind: a flat grey surface with grey text, so a
        disabled primary button does not look like the accent (decision E-852). */
    ButtonDisabled,
    TextDisabled, // text of a button that cannot be used
    Field, // the sunken field of text fields and lists
    Track, // the empty track of sliders and switches
    Knob, // the knob of sliders and switches
    /*! The dark edge around a knob: it marks the knob on the accent fill (3:1) where the knob's
        own light colour does not. */
    KnobOutline,
    BevelLight, // light bevel above and left
    BevelDark, // dark bevel below and right
    /*! The workbench screens (enchanting table and anvil, decision E-884): the parchment of the
        offers and the name field with its hovered and unusable states, the ink written on it
        (runes and letters) and the green of experience levels and costs. On parchment the levels
        take a dark green (a usable offer's light parchment) and a light red (an unusable offer's
        dark parchment). */
    Parchment,
    ParchmentHover, // parchment under the pointer
    ParchmentDisabled, // parchment of an unusable offer
    Ink, // runes and letters on parchment
    InkDisabled, // ink on unusable parchment
    Level, // experience levels and costs
    LevelOnParchment, // levels on usable parchment
    ErrorOnParchment, // levels on unusable parchment
    /*! Fully transparent (no background, no border). */
    Clear,
    RoleCount, // the number of roles, not a colour
};

/*! \brief A colour role with its QML name and colour. */
struct Entry
{
    /*! The role. */
    Role role;
    /*! Property name in QML (Theme.<name>). */
    const char *name;
    /*! Its colour. */
    QRgb argb;
};

/*! \brief Colour variants of the menus (decision E-852; QTCRAFT_THEME chooses one, not a setting
    yet): the classic one (blue gradient, Qt green), one like the Qt web site (near-black
    background, neutral greys, Qt green) and the same with the brighter "Qt Neon" accent.

    Each variant changes only a few roles of the classic palette; the contrast pairs hold for
    every one (test_ui_palette). */
enum class Preset {
    Classic, // blue gradient, Qt green
    QtDark,  // like the Qt web site: near-black background, neutral greys, Qt green
    QtNeon,  // QtDark with the brighter "Qt Neon" accent
};
/*! Every variant, in the order of their names A, B, C. */
inline constexpr Preset presets[] = {Preset::Classic, Preset::QtDark, Preset::QtNeon};
/*! The variant the game starts with (Mensch 2026-10-22: Qt Neon on near-black). */
inline constexpr Preset defaultPreset = Preset::QtNeon;

/*! Chooses the variant \a preset for the whole process; before the QML engine reads \c {Theme}
    (QTCRAFT_THEME). */
void setPreset(Preset preset);
/*! Returns the variant chosen for the process. */
Preset preset();
/*! Returns the variant named \a name: "A"/"classic", "B"/"qt", "C"/"neon" (case does not
    matter); nothing for anything else. */
std::optional<Preset> presetFromName(QStringView name);

/*! Returns the roles with the colours of the chosen variant. */
std::span<const Entry> entries();
/*! Returns the colour of \a role in the chosen variant. */
QColor color(Role role);
/*! Returns the QML name of \a role (Theme.<name>). */
QString name(Role role);

/*! \brief A text colour over its backgrounds: \c {layers} from the world upwards (the first one
    lies directly on the world).

    Large texts (at least 18 pt, or 14 pt bold) need 3:1 instead of 4.5:1. */
struct TextPair
{
    /*! Where the pair appears, for the test's messages. */
    const char *where = nullptr;
    /*! The text colour. */
    Role text = Role::Text;
    /*! The backgrounds it stands on, from the world upwards. */
    QList<Role> layers;
    /*! Whether the text is large (3:1 suffices). */
    bool large = false;
};

/*! Returns the text pairs of every screen. */
QList<TextPair> textPairs();
/*! Returns the worlds the screens appear over: snow, desert sand, sky, water, grass, night,
    cave. */
QList<QColor> worldBackgrounds();

/*! Returns the WCAG 2 contrast ratio of two opaque colours \a a and \a b (1 to 21). */
double contrast(const QColor &a, const QColor &b);
/*! Returns \a over (with alpha) drawn on an opaque \a under. */
QColor composite(const QColor &over, const QColor &under);
/*! Returns the lowest contrast of \a pair over all world backgrounds. */
double worstContrast(const TextPair &pair);

/*! Returns the palette of the Qt Quick Controls (decision E-279), set for the whole application:
    popups, dialogs, fields, combo boxes and their lists, labels and the attached tooltip take
    their colours from it instead of the system palette. The macOS system palette in the dark
    scheme pairs a translucent light tooltip background with black tooltip text and has light
    "alternate base" and "dark" colours, which are unreadable under white text. */
QPalette controlPalette();

/*! \brief Text and background roles of controlPalette() that controls draw together. */
struct ControlPair
{
    /*! The text colour. */
    QPalette::ColorRole text = QPalette::Text;
    /*! The background it stands on. */
    QPalette::ColorRole background = QPalette::Base;
};
/*! Returns the text and background pairs of controlPalette() that the contrast test checks. */
QList<ControlPair> controlPairs();

/*! Contrast normal text needs (WCAG AA). */
inline constexpr double normalTextMinimum = 4.5;
/*! Contrast large text needs (WCAG AA). */
inline constexpr double largeTextMinimum = 3.0;

} // namespace QcUiPalette

Q_DECLARE_TYPEINFO(QcUiPalette::ControlPair, Q_PRIMITIVE_TYPE);

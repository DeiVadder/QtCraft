#pragma once

#include <QColor>
#include <QList>
#include <QPalette>
#include <QString>

#include <span>

// The colours of every game screen in one place. QML reads them as `Theme` (QcUiTheme); screens use
// no colours of their own. The text pairs below say which text colour stands on which backgrounds,
// so one test checks the contrast of all screens over the brightest and darkest world (snow,
// desert, water, night) instead of each screen on its own.
namespace QcUiPalette {

enum class Role {
    // Texts.
    Text,
    TextSecondary,
    TextMuted,
    Heading,
    Link,
    Warning,
    Error,
    // Shadow and outline around texts and icons over the world.
    TextOutline,
    // Backgrounds, from the world upwards.
    HudBackdrop,
    Scrim,
    ScrimStrong,
    DeathScrim,
    Panel,
    // The panel without transparency: screens and dialogs over other screens (ScreenFrame).
    PanelSolid,
    PanelRaised,
    PanelBorder,
    MenuTop,
    MenuBottom,
    // Slots, bars and markers.
    Slot,
    // Hotbar slots lie directly on the world: darker than slots on a panel.
    HudSlot,
    SlotHover,
    SlotBorder,
    // Edge of the colour swatches of the character screen: visible on the panel and around the
    // darkest colours (black hair), 3:1 as WCAG asks of controls.
    SwatchBorder,
    SlotPicked,
    // Faint light behind item icons, so dark items (coal, obsidian) stand out on dark slots.
    IconBackdrop,
    Selection,
    // The ring around the item the pad's focus is on (decision E-583).
    FocusRing,
    // Pad button symbols (decision E-612): the disc behind a symbol and the colours of the face
    // buttons' letters and shapes (plain colours, no logos).
    PadGlyph,
    PadGreen,
    PadRed,
    PadBlue,
    PadYellow,
    PadPink,
    Crosshair,
    Progress,
    Health,
    // The experience bar and level in the HUD (decision E-398).
    Experience,
    Flame,
    FlameCore,
    DurabilityGood,
    DurabilityLow,
    DurabilityBad,
    InfoButton,
    InfoButtonHover,
    // Handbook: separators, the selected row, link chips.
    Separator,
    RowHighlight,
    LinkBackground,
    LinkHover,
    // Tooltips: nearly opaque, so the text reads the same over panels, headings and the world.
    TooltipBackground,
    // Debug display: the frame and tick graphs and their budget line (16.7 ms, 50 ms).
    GraphFrame,
    GraphTick,
    GraphLimit,
    // Dials of a held compass or clock (decision E-769): the compass face and rim, the clock's night
    // and day halves, rim and hand.
    DialFace,
    DialRim,
    ClockNight,
    ClockDay,
    ClockRim,
    ClockHand,
    // The wooden board of the sign editor (decision E-766).
    SignBoard,
    SignBorder,
    // A filled map in the hand (decision E-805): the paper with its edge and the players on it,
    // the own one and the others, and the item frames showing the map (decision E-808).
    MapPaper,
    MapPaperEdge,
    MapMarker,
    MapMarkerOther,
    MapMarkerFrame,
    // Controls of the menus (decision E-845): the accent (Qt green) of the primary button, the
    // filled part of sliders and switches and the chosen category, with the dark text that stands on
    // it (white on Qt green reaches only 2.1:1); the secondary button in its states, the destructive
    // button, the sunken field of text fields and lists, the empty track of sliders and switches
    // (3:1 to the panel, WCAG 1.4.11), the knob of sliders and switches and the bevel that gives
    // buttons their blocky edge (light above and left, dark below and right).
    Accent,
    AccentHover,
    AccentPressed,
    AccentText,
    Button,
    ButtonHover,
    ButtonPressed,
    Danger,
    DangerHover,
    Field,
    Track,
    Knob,
    // The dark edge around a knob: it marks the knob on the accent fill (3:1) where the knob's own
    // light colour does not.
    KnobOutline,
    BevelLight,
    BevelDark,
    // Fully transparent (no background, no border).
    Clear,
    RoleCount,
};

struct Entry
{
    Role role;
    // Property name in QML (Theme.<name>).
    const char *name;
    QRgb argb;
};

std::span<const Entry> entries();
QColor color(Role role);
QString name(Role role);

// A text colour over its backgrounds: `layers` from the world upwards (the first one lies directly
// on the world). Large texts (at least 18 pt, or 14 pt bold) need 3:1 instead of 4.5:1.
struct TextPair
{
    const char *where = nullptr;
    Role text = Role::Text;
    QList<Role> layers;
    bool large = false;
};

QList<TextPair> textPairs();
// Worlds the screens appear over: snow, desert sand, sky, water, grass, night, cave.
QList<QColor> worldBackgrounds();

// WCAG 2 contrast ratio of two opaque colours (1 to 21).
double contrast(const QColor &a, const QColor &b);
// `over` (with alpha) on an opaque `under`.
QColor composite(const QColor &over, const QColor &under);
// The lowest contrast of a pair over all world backgrounds.
double worstContrast(const TextPair &pair);

// The palette of the Qt Quick Controls (decision E-279), set for the whole application: popups,
// dialogs, fields, combo boxes and their lists, labels and the attached tooltip take their colours
// from it instead of the system palette. The macOS system palette in the dark scheme pairs a
// translucent light tooltip background with black tooltip text and has light "alternate base"
// and "dark" colours, which are unreadable under white text.
QPalette controlPalette();

// Text and background roles of controlPalette() that controls draw together.
struct ControlPair
{
    QPalette::ColorRole text = QPalette::Text;
    QPalette::ColorRole background = QPalette::Base;
};
QList<ControlPair> controlPairs();

inline constexpr double normalTextMinimum = 4.5;
inline constexpr double largeTextMinimum = 3.0;

} // namespace QcUiPalette

Q_DECLARE_TYPEINFO(QcUiPalette::ControlPair, Q_PRIMITIVE_TYPE);

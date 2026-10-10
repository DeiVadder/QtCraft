#pragma once

#include <QColor>
#include <QList>
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
    SlotPicked,
    // Faint light behind item icons, so dark items (coal, obsidian) stand out on dark slots.
    IconBackdrop,
    Selection,
    Crosshair,
    Progress,
    Health,
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
    // Debug display: the frame and tick graphs and their budget line (16.7 ms, 50 ms).
    GraphFrame,
    GraphTick,
    GraphLimit,
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
    const char *where;
    Role text;
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

inline constexpr double normalTextMinimum = 4.5;
inline constexpr double largeTextMinimum = 3.0;

} // namespace QcUiPalette

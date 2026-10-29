// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/settings/QcUiPalette.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace QcUiPalette {

namespace {

using R = Role;

constexpr std::array<Entry, static_cast<std::size_t>(Role::RoleCount)> palette = {{
    {R::Text, "text", 0xffffffff},
    {R::TextSecondary, "textSecondary", 0xffe2e2e2},
    {R::TextMuted, "textMuted", 0xffb4b4b4},
    {R::Heading, "heading", 0xffffd27a},
    {R::Link, "link", 0xffa8d4ff},
    {R::Warning, "warning", 0xffffb060},
    {R::Error, "error", 0xffff9a90},
    {R::TextOutline, "textOutline", 0xc0000000},
    {R::HudBackdrop, "hudBackdrop", 0xa0101010},
    {R::Scrim, "scrim", 0x90000000},
    {R::ScrimStrong, "scrimStrong", 0xb0000000},
    {R::DeathScrim, "deathScrim", 0xb0600000},
    {R::Panel, "panel", 0xf0262626},
    {R::PanelSolid, "panelSolid", 0xff262626},
    {R::PanelRaised, "panelRaised", 0xa0303030},
    {R::PanelBorder, "panelBorder", 0x80ffffff},
    {R::MenuTop, "menuTop", 0xff3d6f9e},
    {R::MenuBottom, "menuBottom", 0xff22384f},
    {R::Slot, "slot", 0xa0484848},
    {R::HudSlot, "hudSlot", 0xc8181818},
    {R::SlotHover, "slotHover", 0xc0707070},
    {R::SlotBorder, "slotBorder", 0x80000000},
    {R::SwatchBorder, "swatchBorder", 0xffcfcfcf},
    {R::SlotPicked, "slotPicked", 0xc0000000},
    {R::IconBackdrop, "iconBackdrop", 0x30ffffff},
    {R::Selection, "selection", 0xffffffff},
    {R::FocusRing, "focusRing", 0xffffd27a},
    {R::PadGlyph, "padGlyph", 0xf0202020},
    {R::PadGreen, "padGreen", 0xff6fdc6f},
    {R::PadRed, "padRed", 0xffff7a6e},
    {R::PadBlue, "padBlue", 0xff7ab4ff},
    {R::PadYellow, "padYellow", 0xffffd75a},
    {R::PadPink, "padPink", 0xffff9ad6},
    {R::Crosshair, "crosshair", 0xe0ffffff},
    {R::Progress, "progress", 0xffffffff},
    {R::Health, "health", 0xffe02828},
    {R::Experience, "experience", 0xff80ff20},
    {R::Flame, "flame", 0xffe05010},
    {R::FlameCore, "flameCore", 0xffffe060},
    {R::DurabilityGood, "durabilityGood", 0xff40d040},
    {R::DurabilityLow, "durabilityLow", 0xffe0c030},
    {R::DurabilityBad, "durabilityBad", 0xffe04030},
    {R::StatusGood, "statusGood", 0xff50e650},
    {R::StatusWarn, "statusWarn", 0xffe0c030},
    {R::StatusBad, "statusBad", 0xffffb4ac},
    {R::InfoButton, "infoButton", 0xe0202020},
    {R::InfoButtonHover, "infoButtonHover", 0xf0ffd27a},
    {R::Separator, "separator", 0x40ffffff},
    {R::RowHighlight, "rowHighlight", 0x20ffffff},
    {R::LinkBackground, "linkBackground", 0x26ffffff},
    {R::LinkHover, "linkHover", 0x60507aa8},
    {R::TooltipBackground, "tooltipBackground", 0xf21c1c1c},
    {R::GraphFrame, "graphFrame", 0xff8ee68e},
    {R::GraphTick, "graphTick", 0xff8ecaff},
    {R::GraphLimit, "graphLimit", 0xffffb060},
    {R::DialFace, "dialFace", 0xffd8d4c8},
    {R::DialRim, "dialRim", 0xff5c5c62},
    {R::ClockNight, "clockNight", 0xff2a3550},
    {R::ClockDay, "clockDay", 0xff6a9ae0},
    {R::ClockRim, "clockRim", 0xffd8b850},
    {R::ClockHand, "clockHand", 0xfff0e8a0},
    {R::SignBoard, "signBoard", 0xffa8844e},
    {R::SignBorder, "signBorder", 0xff55401f},
    {R::MapPaper, "mapPaper", 0xffe6dab4},
    {R::MapPaperEdge, "mapPaperEdge", 0xff9c8a62},
    {R::MapMarker, "mapMarker", 0xfff8f8f0},
    {R::MapMarkerOther, "mapMarkerOther", 0xff9cc0f0},
    {R::MapMarkerFrame, "mapMarkerFrame", 0xff3cb83c},
    {R::Accent, "accent", 0xff41cd52},
    {R::AccentHover, "accentHover", 0xff5ad96a},
    {R::AccentPressed, "accentPressed", 0xff34a843},
    {R::AccentText, "accentText", 0xff0b1a0e},
    {R::Button, "button", 0xff3a3d42},
    {R::ButtonHover, "buttonHover", 0xff4a4f57},
    {R::ButtonPressed, "buttonPressed", 0xff2c2e32},
    {R::Danger, "danger", 0xffa8372e},
    {R::DangerHover, "dangerHover", 0xff8e2c24},
    {R::ButtonDisabled, "buttonDisabled", 0xff2e3033},
    {R::TextDisabled, "textDisabled", 0xffc4c4c4},
    {R::Field, "field", 0xff1a1a1a},
    {R::Track, "track", 0xff7c7c7c},
    {R::Knob, "knob", 0xffe8e8e8},
    {R::KnobOutline, "knobOutline", 0xff1a1a1a},
    {R::BevelLight, "bevelLight", 0x30ffffff},
    {R::BevelDark, "bevelDark", 0x80000000},
    {R::Parchment, "parchment", 0xffe3cf9c},
    {R::ParchmentHover, "parchmentHover", 0xfff1e2b8},
    {R::ParchmentDisabled, "parchmentDisabled", 0xff5e4e3a},
    {R::Ink, "ink", 0xff3a2414},
    {R::InkDisabled, "inkDisabled", 0xffbcaa8a},
    {R::Level, "level", 0xff8cf04a},
    {R::LevelOnParchment, "levelOnParchment", 0xff2a5e10},
    {R::ErrorOnParchment, "errorOnParchment", 0xffffb4ac},
    {R::Clear, "clear", 0x00000000},
}};

// A role whose colour a variant changes.
struct Override
{
    Role role;
    QRgb argb;
};

// Like the Qt web site: a near-black background fading to black, neutral greys without the blue
// tint of the classic buttons and link chips.
constexpr std::array<Override, 8> qtDark = {{
    {R::MenuTop, 0xff0c121c},
    {R::MenuBottom, 0xff000000},
    {R::Button, 0xff3c3c3c},
    {R::ButtonHover, 0xff4e4e4e},
    {R::ButtonPressed, 0xff2c2c2c},
    {R::ButtonDisabled, 0xff303030},
    {R::TextDisabled, 0xff9a9a9a},
    {R::LinkHover, 0x60686868},
}};

// The Qt Neon accent (#2CDE85) with its states; dark text on it as on Qt green.
constexpr std::array<Override, 4> qtNeon = {{
    {R::Accent, 0xff2cde85},
    {R::AccentHover, 0xff5ae8a2},
    {R::AccentPressed, 0xff22b36b},
    {R::AccentText, 0xff0b1a0e},
}};

Preset activePreset = defaultPreset;

std::array<Entry, static_cast<std::size_t>(Role::RoleCount)> makeEntries(Preset preset)
{
    std::array<Entry, static_cast<std::size_t>(Role::RoleCount)> made = palette;
    const auto apply = [&made](std::span<const Override> overrides) {
        for (const Override &change : overrides) {
            made[static_cast<std::size_t>(change.role)].argb = change.argb;
        }
    };
    if (preset != Preset::Classic) {
        apply(qtDark);
    }
    if (preset == Preset::QtNeon) {
        apply(qtNeon);
    }
    return made;
}

std::array<Entry, static_cast<std::size_t>(Role::RoleCount)> activeEntries = makeEntries(defaultPreset);

double channel(int value)
{
    const double c = value / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double luminance(const QColor &color)
{
    return 0.2126 * channel(color.red()) + 0.7152 * channel(color.green()) + 0.0722 * channel(color.blue());
}

} // namespace

void setPreset(Preset preset)
{
    activePreset = preset;
    activeEntries = makeEntries(preset);
}

Preset preset()
{
    return activePreset;
}

std::optional<Preset> presetFromName(QStringView name)
{
    const QString lower = name.toString().toLower();
    if (lower == u"a" || lower == u"classic") {
        return Preset::Classic;
    }
    if (lower == u"b" || lower == u"qt") {
        return Preset::QtDark;
    }
    if (lower == u"c" || lower == u"neon") {
        return Preset::QtNeon;
    }
    return std::nullopt;
}

std::span<const Entry> entries()
{
    return activeEntries;
}

QColor color(Role role)
{
    return QColor::fromRgba(activeEntries[static_cast<std::size_t>(role)].argb);
}

QString name(Role role)
{
    return QString::fromLatin1(activeEntries[static_cast<std::size_t>(role)].name);
}

namespace {

// The text pairs of the workbench screens (decision E-884): runes and letters on the parchment of an
// offer or the name field, levels on the panel and on the parchment of usable and unusable offers.
QList<TextPair> workbenchPairs()
{
    return {
        {"Enchanting offers and anvil name", R::Ink, {R::Scrim, R::Panel, R::Parchment}},
        {"Hovered enchanting offer", R::Ink, {R::Scrim, R::Panel, R::ParchmentHover}},
        {"Level costs", R::Level, {R::Scrim, R::Panel}},
        {"Lacking level costs", R::Error, {R::Scrim, R::Panel}},
        {"Level of an enchanting offer", R::LevelOnParchment, {R::Scrim, R::Panel, R::Parchment}},
        {"Level of a hovered enchanting offer", R::LevelOnParchment, {R::Scrim, R::Panel, R::ParchmentHover}},
        {"Lacking level of an unusable enchanting offer", R::ErrorOnParchment, {R::Scrim, R::Panel, R::ParchmentDisabled}},
        // Unusable controls need no contrast (WCAG 1.4.3), but the runes should stay visible.
        {"Runes of an unusable enchanting offer", R::InkDisabled, {R::Scrim, R::Panel, R::ParchmentDisabled}, true},
    };
}

} // namespace

QList<TextPair> textPairs()
{
    QList<TextPair> pairs{
        {"HUD texts (debug line, target, time, chat, hints)", R::Text, {R::HudBackdrop}},
        {"Screen titles over the dimmed world (pause, death)", R::Text, {R::Scrim}, true},
        {"Death screen", R::Text, {R::DeathScrim}, true},
        {"Death screen note", R::TextSecondary, {R::DeathScrim}},
        {"Panel text (inventory, chest, furnace, handbook)", R::Text, {R::Scrim, R::Panel}},
        {"Panel secondary text", R::TextSecondary, {R::Scrim, R::Panel}},
        {"Panel hints", R::TextMuted, {R::Scrim, R::Panel}},
        {"Handbook headings", R::Heading, {R::ScrimStrong, R::Panel}, true},
        {"Handbook links", R::Link, {R::ScrimStrong, R::Panel, R::PanelRaised}},
        {"Handbook machine recipes", R::Warning, {R::ScrimStrong, R::Panel}},
        {"Handbook facts", R::TextSecondary, {R::ScrimStrong, R::Panel, R::PanelRaised}},
        {"Slot count", R::Text, {R::Scrim, R::Panel, R::Slot, R::IconBackdrop}},
        {"Hovered slot count", R::Text, {R::Scrim, R::Panel, R::SlotHover}},
        {"Picked-up stack count at the cursor", R::Text, {R::Scrim, R::Panel, R::SlotPicked}},
        {"Hotbar count", R::Text, {R::HudSlot, R::IconBackdrop}},
        {"Main menu text", R::Text, {R::MenuTop}},
        {"Main menu text, lower half", R::Text, {R::MenuBottom}},
        {"Main menu world details", R::TextSecondary, {R::MenuTop, R::Scrim}},
        {"Main menu errors", R::Error, {R::MenuTop, R::Scrim}},
        // Tooltips appear anywhere: over the world as well as over panels.
        {"Tooltips", R::Text, {R::TooltipBackground}},
        {"Tooltips over a panel", R::Text, {R::Scrim, R::Panel, R::TooltipBackground}},
        {"Loading and error message", R::Text, {R::ScrimStrong}, true},
        // Lines are graphics, not text: 3:1 like large text (WCAG 1.4.11).
        {"Debug frame graph", R::GraphFrame, {R::HudBackdrop}, true},
        {"Debug tick graph", R::GraphTick, {R::HudBackdrop}, true},
        {"Debug graph budget line", R::GraphLimit, {R::HudBackdrop}, true},
        // Signal bars of the player list (decision E-886), graphics as well.
        {"Status good", R::StatusGood, {R::HudBackdrop}, true},
        {"Status warning", R::StatusWarn, {R::HudBackdrop}, true},
        {"Status bad", R::StatusBad, {R::HudBackdrop}, true},
        // Pad button symbols are small shapes and letters on their own disc (decision E-612).
        {"Pad button symbols", R::Text, {R::PadGlyph}, true},
        {"Pad button, green", R::PadGreen, {R::PadGlyph}, true},
        {"Pad button, red", R::PadRed, {R::PadGlyph}, true},
        {"Pad button, blue", R::PadBlue, {R::PadGlyph}, true},
        {"Pad button, yellow", R::PadYellow, {R::PadGlyph}, true},
        {"Pad button, pink", R::PadPink, {R::PadGlyph}, true},
        // Controls of the menus (decision E-845); their surfaces are opaque.
        {"Primary button", R::AccentText, {R::Accent}},
        {"Primary button, hovered", R::AccentText, {R::AccentHover}},
        {"Primary button, pressed", R::AccentText, {R::AccentPressed}},
        {"Secondary button", R::Text, {R::Button}},
        {"Secondary button, hovered", R::Text, {R::ButtonHover}},
        {"Secondary button, pressed", R::Text, {R::ButtonPressed}},
        {"Destructive button", R::Text, {R::Danger}},
        {"Destructive button, hovered", R::Text, {R::DangerHover}},
        {"Disabled button", R::TextDisabled, {R::ButtonDisabled}},
        // A disabled quiet button has no surface: its text lies on the menu, a screen or the panel.
        // Disabled controls are exempt from WCAG 1.4.3; 3:1 keeps the label legible (the classic
        // menu's light blue needs a lighter grey than the dark variants).
        {"Disabled quiet button on the menu", R::TextDisabled, {R::MenuTop}, true},
        {"Disabled quiet button on the menu, lower half", R::TextDisabled, {R::MenuBottom}, true},
        {"Disabled quiet button on a screen", R::TextDisabled, {R::PanelSolid}, true},
        {"Disabled quiet button on the panel", R::TextDisabled, {R::Scrim, R::Panel}, true},
        {"Fields and lists", R::Text, {R::Field}},
        {"Screens and dialogs (ScreenFrame)", R::Text, {R::PanelSolid}},
        {"Highlighted row of a drop-down list", R::AccentText, {R::Accent}},
        // Graphics, not text: the empty track against the panel, the filled part against the track,
        // the knob on the track and its dark outline on the fill (WCAG 1.4.11, 3:1).
        {"Slider track on the panel", R::Track, {R::Scrim, R::Panel, R::PanelRaised}, true},
        {"Slider fill on the panel", R::Accent, {R::Scrim, R::Panel, R::PanelRaised}, true},
        {"Knob on the slider track", R::Knob, {R::Track}, true},
        {"Knob outline on the slider fill and a switch that is on", R::KnobOutline, {R::Accent}, true},
        {"Knob outline on the slider track and a switch that is off", R::KnobOutline, {R::Track}, true},
        {"Check box outline on the panel", R::Track, {R::PanelSolid, R::PanelRaised}, true},
    };
    pairs += workbenchPairs();
    return pairs;
}

QList<QColor> worldBackgrounds()
{
    return {
        QColor(255, 255, 255), // snow in sunlight
        QColor(219, 207, 158), // desert sand
        QColor(168, 204, 242), // day sky at the horizon
        QColor(47, 95, 176),   // water
        QColor(98, 160, 64),   // grass
        QColor(5, 7, 15),      // night sky
        QColor(0, 0, 0),       // cave
    };
}

QPalette controlPalette()
{
    using P = QPalette;
    const QColor tooltip = color(R::TooltipBackground);
    const QList<std::pair<P::ColorRole, QColor>> roles = {
        {P::Window, QColor(0x26, 0x26, 0x26)},
        {P::WindowText, color(R::Text)},
        {P::Base, color(R::Field)},
        {P::AlternateBase, QColor(0x30, 0x30, 0x30)},
        {P::Text, color(R::Text)},
        {P::PlaceholderText, color(R::TextMuted)},
        {P::Button, color(R::Button)},
        {P::ButtonText, color(R::Text)},
        // The Basic style draws a highlighted (primary) button, the filled part of a slider, a
        // switch that is on and a progress bar in Dark, the text of a highlighted button in
        // BrightText: both are the accent and its text, as in the own controls (decision E-845).
        {P::BrightText, color(R::AccentText)},
        // HighlightedText stands on two surfaces: selected text on Highlight and the highlighted
        // row of a list or a ComboBox popup (ItemDelegate) on Light. No colour reads on both Light
        // and the bright accent, so Highlight is a dark green that white text reads on (with the
        // Qt Neon accent the petrol #00414A of the Qt web site).
        {P::Highlight, activePreset == Preset::QtNeon ? QColor(0x00, 0x41, 0x4a) : QColor(0x26, 0x80, 0x32)},
        {P::HighlightedText, color(R::Text)},
        {P::ToolTipBase, QColor(tooltip.red(), tooltip.green(), tooltip.blue())},
        {P::ToolTipText, color(R::Text)},
        {P::Light, QColor(0x3c, 0x3c, 0x3c)},
        {P::Midlight, QColor(0x36, 0x36, 0x36)},
        {P::Mid, QColor(0x8a, 0x8a, 0x8a)},
        {P::Dark, color(R::Accent)},
        {P::Shadow, QColor(0, 0, 0)},
        {P::Link, color(R::Link)},
        {P::LinkVisited, color(R::Link)},
        {P::Accent, color(R::Accent)},
    };

    QPalette result;
    for (const auto &[role, value] : roles) {
        result.setColor(role, value);
    }
    // Disabled texts are dimmed but stay legible.
    for (const P::ColorRole role : {P::WindowText, P::Text, P::ButtonText}) {
        result.setColor(P::Disabled, role, color(R::TextMuted).darker(130));
    }
    return result;
}

QList<ControlPair> controlPairs()
{
    using P = QPalette;
    return {
        {P::WindowText, P::Window},
        {P::Text, P::Base},
        {P::Text, P::AlternateBase},
        {P::ButtonText, P::Button},
        {P::HighlightedText, P::Highlight},
        {P::HighlightedText, P::Light},
        {P::BrightText, P::Dark},
        {P::ToolTipText, P::ToolTipBase},
        {P::PlaceholderText, P::Base},
        {P::Link, P::Window},
    };
}

double contrast(const QColor &a, const QColor &b)
{
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

QColor composite(const QColor &over, const QColor &under)
{
    const double alpha = over.alphaF();
    const auto mix = [alpha](int top, int bottom) { return static_cast<int>(std::lround(top * alpha + bottom * (1.0 - alpha))); };
    return QColor(mix(over.red(), under.red()), mix(over.green(), under.green()), mix(over.blue(), under.blue()));
}

double worstContrast(const TextPair &pair)
{
    double worst = std::numeric_limits<double>::max();
    const QList<QColor> worlds = worldBackgrounds();
    for (const QColor &world : worlds) {
        QColor background = world;
        for (const Role layer : pair.layers) {
            background = composite(color(layer), background);
        }
        worst = std::min(worst, contrast(composite(color(pair.text), background), background));
    }
    return worst;
}

} // namespace QcUiPalette

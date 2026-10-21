#include "clientcore/settings/QcUiPalette.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace QcUiPalette {

namespace {

using R = Role;

constexpr std::array<Entry, std::size_t(Role::RoleCount)> palette = {{
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
    {R::Clear, "clear", 0x00000000},
}};

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

std::span<const Entry> entries()
{
    return palette;
}

QColor color(Role role)
{
    return QColor::fromRgba(palette[std::size_t(role)].argb);
}

QString name(Role role)
{
    return QString::fromLatin1(palette[std::size_t(role)].name);
}

QList<TextPair> textPairs()
{
    return {
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
        // Pad button symbols are small shapes and letters on their own disc (decision E-612).
        {"Pad button symbols", R::Text, {R::PadGlyph}, true},
        {"Pad button, green", R::PadGreen, {R::PadGlyph}, true},
        {"Pad button, red", R::PadRed, {R::PadGlyph}, true},
        {"Pad button, blue", R::PadBlue, {R::PadGlyph}, true},
        {"Pad button, yellow", R::PadYellow, {R::PadGlyph}, true},
        {"Pad button, pink", R::PadPink, {R::PadGlyph}, true},
    };
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
        {P::Base, QColor(0x1a, 0x1a, 0x1a)},
        {P::AlternateBase, QColor(0x30, 0x30, 0x30)},
        {P::Text, color(R::Text)},
        {P::PlaceholderText, color(R::TextMuted)},
        {P::Button, QColor(0x32, 0x32, 0x32)},
        {P::ButtonText, color(R::Text)},
        {P::BrightText, color(R::Text)},
        {P::Highlight, color(R::MenuTop)},
        {P::HighlightedText, color(R::Text)},
        {P::ToolTipBase, QColor(tooltip.red(), tooltip.green(), tooltip.blue())},
        {P::ToolTipText, color(R::Text)},
        {P::Light, QColor(0x3c, 0x3c, 0x3c)},
        {P::Midlight, QColor(0x36, 0x36, 0x36)},
        {P::Mid, QColor(0x8a, 0x8a, 0x8a)},
        {P::Dark, QColor(0x1a, 0x1a, 0x1a)},
        {P::Shadow, QColor(0, 0, 0)},
        {P::Link, color(R::Link)},
        {P::LinkVisited, color(R::Link)},
        {P::Accent, color(R::MenuTop)},
    };

    QPalette palette;
    for (const auto &[role, value] : roles) {
        palette.setColor(role, value);
    }
    // Disabled texts are dimmed but stay legible.
    for (const P::ColorRole role : {P::WindowText, P::Text, P::ButtonText, P::HighlightedText}) {
        palette.setColor(P::Disabled, role, color(R::TextMuted).darker(130));
    }
    return palette;
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
    const auto mix = [alpha](int top, int bottom) { return int(std::lround(top * alpha + bottom * (1.0 - alpha))); };
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

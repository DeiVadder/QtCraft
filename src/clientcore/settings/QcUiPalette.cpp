#include "QcUiPalette.h"

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
    {R::Crosshair, "crosshair", 0xe0ffffff},
    {R::Progress, "progress", 0xffffffff},
    {R::Health, "health", 0xffe02828},
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
    {R::GraphFrame, "graphFrame", 0xff8ee68e},
    {R::GraphTick, "graphTick", 0xff8ecaff},
    {R::GraphLimit, "graphLimit", 0xffffb060},
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
        {"Loading and error message", R::Text, {R::ScrimStrong}, true},
        // Lines are graphics, not text: 3:1 like large text (WCAG 1.4.11).
        {"Debug frame graph", R::GraphFrame, {R::HudBackdrop}, true},
        {"Debug tick graph", R::GraphTick, {R::HudBackdrop}, true},
        {"Debug graph budget line", R::GraphLimit, {R::HudBackdrop}, true},
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

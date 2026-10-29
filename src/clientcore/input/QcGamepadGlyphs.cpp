// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/input/QcGamepadGlyphs.h"

#include <array>

using namespace Qt::StringLiterals;

namespace QcGamepadGlyphs {

namespace {

// The inputs besides the buttons.
enum class Extra { LeftTrigger, RightTrigger, LeftStickMove, RightStickMove };

struct Input
{
    std::optional<QcGamepadButton> button;
    Extra extra = Extra::LeftTrigger;
};

std::optional<Input> inputFromName(const QString &name)
{
    if (const std::optional<QcGamepadButton> button = QcGamepadNames::buttonFromName(name)) {
        return Input{.button = button, .extra = Extra::LeftTrigger};
    }
    static const std::array<std::pair<QStringView, Extra>, 4> extras{{
        {u"leftTrigger", Extra::LeftTrigger},
        {u"rightTrigger", Extra::RightTrigger},
        {u"leftStickMove", Extra::LeftStickMove},
        {u"rightStickMove", Extra::RightStickMove},
    }};
    for (const auto &[extraName, extra] : extras) {
        if (extraName == name) {
            return Input{.button = std::nullopt, .extra = extra};
        }
    }
    return std::nullopt;
}

// The labels of one family: face buttons south, east, west, north with their colours, back,
// start, shoulders, triggers and stick clicks.
struct Labels
{
    std::array<QStringView, 4> faces;
    std::array<QStringView, 4> colours;
    QStringView back;
    QStringView start;
    std::array<QStringView, 2> shoulders;
    std::array<QStringView, 2> triggers;
    std::array<QStringView, 2> stickClicks;
};

const Labels &labelsOf(QcGamepadFamily family)
{
    static const Labels xbox{{u"A", u"B", u"X", u"Y"}, {u"green", u"red", u"blue", u"yellow"}, u"View", u"Menu",
                             {u"LB", u"RB"},           {u"LT", u"RT"},                       {u"LS", u"RS"}};
    static const Labels playStation{{u"", u"", u"", u""}, {u"blue", u"red", u"pink", u"green"}, u"Share", u"Options",
                                    {u"L1", u"R1"},        {u"L2", u"R2"},                       {u"L3", u"R3"}};
    // Nintendo's letters sit in other places: B at the bottom, A on the right.
    static const Labels nintendo{{u"B", u"A", u"Y", u"X"}, {u"", u"", u"", u""}, u"−", u"+", {u"L", u"R"}, {u"ZL", u"ZR"}, {u"LS", u"RS"}};
    static const Labels generic{{u"", u"", u"", u""}, {u"", u"", u"", u""}, u"Select", u"Start", {u"L1", u"R1"}, {u"L2", u"R2"}, {u"L3", u"R3"}};
    switch (family) {
    case QcGamepadFamily::Xbox:
        return xbox;
    case QcGamepadFamily::PlayStation:
        return playStation;
    case QcGamepadFamily::Nintendo:
        return nintendo;
    case QcGamepadFamily::Generic:
        break;
    }
    return generic;
}

Glyph glyphOf(Shape shape, QString label, QString colour, QString direction)
{
    return {.shape = shape, .label = std::move(label), .colour = std::move(colour), .direction = std::move(direction)};
}

constexpr std::array<Shape, 4> playStationShapes{Shape::Cross, Shape::Circle, Shape::Square, Shape::Triangle};
constexpr std::array<QStringView, 4> directions{u"down", u"right", u"left", u"up"};

Glyph faceGlyph(QcGamepadFamily family, std::size_t face)
{
    const Labels &labels = labelsOf(family);
    if (family == QcGamepadFamily::PlayStation) {
        return glyphOf(playStationShapes[face], {}, labels.colours[face].toString(), {});
    }
    if (family == QcGamepadFamily::Generic) {
        return glyphOf(Shape::Position, {}, {}, directions[face].toString());
    }
    return glyphOf(Shape::Label, labels.faces[face].toString(), labels.colours[face].toString(), {});
}

Glyph buttonGlyph(QcGamepadFamily family, QcGamepadButton button)
{
    const Labels &labels = labelsOf(family);
    switch (button) {
    case QcGamepadButton::South:
    case QcGamepadButton::East:
    case QcGamepadButton::West:
    case QcGamepadButton::North:
        return faceGlyph(family, static_cast<std::size_t>(button) - static_cast<std::size_t>(QcGamepadButton::South));
    case QcGamepadButton::Back:
        return glyphOf(Shape::Label, labels.back.toString(), {}, {});
    case QcGamepadButton::Guide:
        return glyphOf(Shape::Label, u"Home"_s, {}, {});
    case QcGamepadButton::Start:
        return glyphOf(Shape::Label, labels.start.toString(), {}, {});
    case QcGamepadButton::LeftStick:
        return glyphOf(Shape::Stick, labels.stickClicks[0].toString(), {}, {});
    case QcGamepadButton::RightStick:
        return glyphOf(Shape::Stick, labels.stickClicks[1].toString(), {}, {});
    case QcGamepadButton::LeftShoulder:
        return glyphOf(Shape::Shoulder, labels.shoulders[0].toString(), {}, {});
    case QcGamepadButton::RightShoulder:
        return glyphOf(Shape::Shoulder, labels.shoulders[1].toString(), {}, {});
    case QcGamepadButton::DpadUp:
        return glyphOf(Shape::Dpad, {}, {}, u"up"_s);
    case QcGamepadButton::DpadDown:
        return glyphOf(Shape::Dpad, {}, {}, u"down"_s);
    case QcGamepadButton::DpadLeft:
        return glyphOf(Shape::Dpad, {}, {}, u"left"_s);
    case QcGamepadButton::DpadRight:
    case QcGamepadButton::Count:
        break;
    }
    return glyphOf(Shape::Dpad, {}, {}, u"right"_s);
}

Glyph extraGlyph(QcGamepadFamily family, Extra extra)
{
    const Labels &labels = labelsOf(family);
    switch (extra) {
    case Extra::LeftTrigger:
        return glyphOf(Shape::Trigger, labels.triggers[0].toString(), {}, {});
    case Extra::RightTrigger:
        return glyphOf(Shape::Trigger, labels.triggers[1].toString(), {}, {});
    case Extra::LeftStickMove:
        return glyphOf(Shape::Stick, u"L"_s, {}, {});
    case Extra::RightStickMove:
        break;
    }
    return glyphOf(Shape::Stick, u"R"_s, {}, {});
}

// The d-pad's arrow in running text.
QString arrowOf(const QString &direction)
{
    static const std::array<std::pair<QStringView, QStringView>, 4> arrows{{{u"up", u"▲"}, {u"down", u"▼"}, {u"left", u"◀"}, {u"right", u"▶"}}};
    for (const auto &[name, arrow] : arrows) {
        if (name == direction) {
            return arrow.toString();
        }
    }
    return {};
}

} // namespace

QcGamepadFamily family(QcGamepadType type)
{
    switch (type) {
    case QcGamepadType::Xbox:
        return QcGamepadFamily::Xbox;
    case QcGamepadType::PlayStation:
        return QcGamepadFamily::PlayStation;
    case QcGamepadType::SwitchPro:
        return QcGamepadFamily::Nintendo;
    case QcGamepadType::Generic:
        break;
    }
    return QcGamepadFamily::Generic;
}

QString familyName(QcGamepadFamily family)
{
    switch (family) {
    case QcGamepadFamily::Xbox:
        return u"xbox"_s;
    case QcGamepadFamily::PlayStation:
        return u"playstation"_s;
    case QcGamepadFamily::Nintendo:
        return u"nintendo"_s;
    case QcGamepadFamily::Generic:
        break;
    }
    return u"generic"_s;
}

std::optional<QcGamepadFamily> familyFromName(const QString &name)
{
    for (const QcGamepadFamily family : {QcGamepadFamily::Generic, QcGamepadFamily::Xbox, QcGamepadFamily::PlayStation, QcGamepadFamily::Nintendo}) {
        if (familyName(family) == name) {
            return family;
        }
    }
    return std::nullopt;
}

QString shapeName(Shape shape)
{
    static const std::array<QStringView, 10> names{u"label", u"cross",    u"circle", u"square", u"triangle",
                                                   u"position", u"shoulder", u"trigger", u"stick", u"dpad"};
    return names[static_cast<std::size_t>(shape)].toString();
}

std::optional<Glyph> glyph(QcGamepadFamily family, const QString &input)
{
    const std::optional<Input> parsed = inputFromName(input);
    if (!parsed) {
        return std::nullopt;
    }
    return parsed->button ? buttonGlyph(family, *parsed->button) : extraGlyph(family, parsed->extra);
}

QString text(QcGamepadFamily family, const QString &input)
{
    const std::optional<Glyph> found = glyph(family, input);
    if (!found) {
        return {};
    }

    switch (found->shape) {
    case Shape::Cross:
        return u"✕"_s;
    case Shape::Circle:
        return u"○"_s;
    case Shape::Square:
        return u"□"_s;
    case Shape::Triangle:
        return u"△"_s;
    case Shape::Dpad:
        return arrowOf(found->direction);
    case Shape::Position:
        return {};
    default:
        return found->label;
    }
}

} // namespace QcGamepadGlyphs

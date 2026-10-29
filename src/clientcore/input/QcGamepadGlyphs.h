// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/input/QcGamepadState.h"

#include <QString>

#include <optional>

// The family whose labels a pad's buttons wear (decision E-612): Xbox letters, PlayStation
// shapes, Nintendo letters in their own places, or positions for pads of unknown make.
enum class QcGamepadFamily {
    Generic,
    Xbox,
    PlayStation,
    Nintendo,
};

// How the buttons of a family are drawn in hints and named in help texts (decision E-612), as
// plain shapes and letters, no logos: a shape (a disc with a letter, the four PlayStation shapes,
// a shoulder or trigger tab, a stick, the d-pad cross with one arm lit, a diamond of four dots with
// one lit for the face buttons of a generic pad), its text and a colour role. QML draws them
// (PadGlyph.qml). The inputs are named like the control interface's buttons ("south", "dpadLeft",
// "leftShoulder", ...), plus "leftTrigger"/"rightTrigger" and "leftStickMove"/"rightStickMove"
// for the triggers and the tilted sticks.
namespace QcGamepadGlyphs {

enum class Shape {
    // A disc with a letter or a word (face buttons, Back, Start, Guide).
    Label,
    Cross,
    Circle,
    Square,
    Triangle,
    // A diamond of four dots, the one in `direction` lit (face buttons of a generic pad).
    Position,
    Shoulder,
    Trigger,
    // A stick, clicked or tilted, with its letter.
    Stick,
    // The d-pad cross with the arm in `direction` lit.
    Dpad,
};

struct Glyph
{
    Shape shape = Shape::Label;
    // Drawn in the shape (empty for the PlayStation shapes, the position and the d-pad).
    QString label;
    // "green", "red", "blue", "yellow", "pink" or empty for the theme's text colour.
    QString colour;
    // "up", "down", "left" or "right" for the d-pad and the position shape, else empty.
    QString direction;

    bool operator==(const Glyph &other) const = default;
};

QcGamepadFamily family(QcGamepadType type);
// "generic", "xbox", "playstation", "nintendo".
QString familyName(QcGamepadFamily family);
std::optional<QcGamepadFamily> familyFromName(const QString &name);
QString shapeName(Shape shape);

// The glyph of an input (names above); nullopt for an unknown name.
std::optional<Glyph> glyph(QcGamepadFamily family, const QString &input);
// The input in running text: the glyph's label, the PlayStation shapes as ✕ ○ □ △, the d-pad as
// ▲ ▼ ◀ ▶. Empty for the face buttons of a generic pad (the caller words their position) and
// unknown names.
QString text(QcGamepadFamily family, const QString &input);

} // namespace QcGamepadGlyphs

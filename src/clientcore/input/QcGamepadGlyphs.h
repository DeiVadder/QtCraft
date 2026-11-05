// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/input/QcGamepadState.h"

#include <QString>

#include <optional>

/*! \brief The family whose labels a pad's buttons wear (decision E-612): Xbox letters,
    PlayStation shapes, Nintendo letters in their own places, or positions for pads of unknown
    make. */
enum class QcGamepadFamily {
    Generic,     // positions for pads of unknown make
    Xbox,        // Xbox letters
    PlayStation, // PlayStation shapes
    Nintendo,    // Nintendo letters in their own places
};

/*! \brief How the buttons of a family are drawn in hints and named in help texts (decision
    E-612), as plain shapes and letters, no logos: a shape (a disc with a letter, the four
    PlayStation shapes, a shoulder or trigger tab, a stick, the d-pad cross with one arm lit, a
    diamond of four dots with one lit for the face buttons of a generic pad), its text and a colour
    role.

    The game draws them (QcPadGlyphPainter in PadGlyph.qml). The inputs are named like the
    control interface's buttons ("south", "dpadLeft", "leftShoulder", ...), plus
    "leftTrigger"/"rightTrigger" and "leftStickMove"/"rightStickMove" for the triggers and the
    tilted sticks. */
namespace QcGamepadGlyphs {

/*! The shape a glyph is drawn as. */
enum class Shape {
    /*! A disc with a letter or a word (face buttons, Back, Start, Guide). */
    Label,
    Cross,    // the PlayStation cross
    Circle,   // the PlayStation circle
    Square,   // the PlayStation square
    Triangle, // the PlayStation triangle
    /*! A diamond of four dots, the one in \c {direction} lit (face buttons of a generic pad). */
    Position,
    Shoulder, // a shoulder tab with its letter
    Trigger,  // a trigger tab with its letter
    /*! A stick, clicked or tilted, with its letter. */
    Stick,
    /*! The d-pad cross with the arm in \c {direction} lit. */
    Dpad,
};

/*! \brief A glyph: its shape, the text in it, its colour role and the lit direction. */
struct Glyph
{
    /*! The shape. */
    Shape shape = Shape::Label;
    /*! Drawn in the shape (empty for the PlayStation shapes, the position and the d-pad). */
    QString label;
    /*! "green", "red", "blue", "yellow", "pink" or empty for the theme's text colour. */
    QString colour;
    /*! "up", "down", "left" or "right" for the d-pad and the position shape, else empty. */
    QString direction;

    bool operator==(const Glyph &other) const = default;
};

/*! Returns the family of the labels on a pad of \a type. */
QcGamepadFamily family(QcGamepadType type);
/*! Returns the name of \a family: "generic", "xbox", "playstation" or "nintendo". */
QString familyName(QcGamepadFamily family);
/*! Returns the family named \a name (familyName()); nullopt for an unknown name. */
std::optional<QcGamepadFamily> familyFromName(const QString &name);

/*! Returns the glyph of \a input (names above) on a pad of \a family; nullopt for an unknown
    name. */
std::optional<Glyph> glyph(QcGamepadFamily family, const QString &input);
/*! Returns \a input of a pad of \a family in running text: the glyph's label, the PlayStation
    shapes as ✕ ○ □ △, the d-pad as ▲ ▼ ◀ ▶. Empty for the face buttons of a generic pad (the
    caller words their position) and unknown names. */
QString text(QcGamepadFamily family, const QString &input);

} // namespace QcGamepadGlyphs

// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The first-person swing (decision E-881): arm and held item move together as one rigid body,
// nothing bends or turns in the fist. A swing lifts the tool a little (wind-up), strikes from the
// lower right corner towards the middle of the screen with growing speed (ease-in), stops on the
// block with a short recoil and settles back to rest. Pure functions of the progress, so the
// motion is testable.
namespace QcHandSwing {

// Mining loops the full swing while the button is held; a single click (attack, placing, eating)
// makes a short one.
enum class Style {
    Mining,
    Click,
};

// The turn of arm and item about the shoulder in degrees, in the camera's frame: `pitch` positive
// lifts the arm (its forward end up), negative strikes down; `yaw` positive turns it inwards
// towards the crosshair; `roll` positive turns it in the picture's plane, counter-clockwise as
// seen, so the head of a tool held in the lower right corner comes down and to the left, towards
// the lower middle of the screen.
struct Angles
{
    double pitch = 0.0;
    double yaw = 0.0;
    double roll = 0.0;
    // How far into the strike (0 above rest, 1 at its lowest point): the hand moves with it
    // (QcAnimator::animateHand).
    double reach = 0.0;
};

// One swing of `style` at `progress` (0..1, clamped). It begins and ends at rest with no speed, so
// mining's loop of swings runs on without a jerk where one ends and the next begins.
[[nodiscard]] Angles at(double progress, Style style);

// The phases of a swing: the progress at which the wind-up ends (highest point), the strike ends
// (lowest point, the blow) and the recoil ends, and the pitch at those points.
struct Shape
{
    double windUpEnd = 0.0;
    double strikeEnd = 0.0;
    double recoilEnd = 0.0;
    double lift = 0.0;
    double strike = 0.0;
    double recoil = 0.0;
};

[[nodiscard]] const Shape &shapeOf(Style style);

} // namespace QcHandSwing

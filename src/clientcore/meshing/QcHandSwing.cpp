#include "clientcore/meshing/QcHandSwing.h"

#include <algorithm>

namespace QcHandSwing {

namespace {

// Mining: a clear lift, then the strike, a recoil of a quarter and nearly half the swing to settle,
// so the loop reads as one firm blow after another. The strike's pitch is small: its travel comes
// from the turn inwards and in the picture's plane and the hand's move (`reach`). A click: the
// same arc, flatter and over sooner.
constexpr Shape miningShape{0.22, 0.42, 0.55, 12.0, -4.0, -3.0};
constexpr Shape clickShape{0.12, 0.32, 0.45, 5.0, -3.0, -2.0};

// At the bottom of the strike (reach 1) the arm is turned this far inwards and in the picture's
// plane: the blow comes from the lower right corner to the left, towards the lower middle of the
// screen, above the hotbar.
constexpr double strikeYaw = 20.0;
constexpr double strikeRoll = 8.0;

// Smoothstep: starts and ends without speed.
double smooth(double t)
{
    return t * t * (3.0 - 2.0 * t);
}

// `weight` (0..1, already eased) of the way from `from` to `to`.
double blend(double from, double to, double weight)
{
    return from + (to - from) * weight;
}

double pitchAt(double p, const Shape &shape)
{
    if (p < shape.windUpEnd) {
        return blend(0.0, shape.lift, smooth(p / shape.windUpEnd));
    }
    if (p < shape.strikeEnd) {
        // Ease-in: the strike gains speed until it meets the block.
        const double t = (p - shape.windUpEnd) / (shape.strikeEnd - shape.windUpEnd);
        return blend(shape.lift, shape.strike, t * t);
    }
    if (p < shape.recoilEnd) {
        // Ease-out: bounced back off the block, slowing down.
        const double t = (p - shape.strikeEnd) / (shape.recoilEnd - shape.strikeEnd);
        return blend(shape.strike, shape.recoil, 1.0 - (1.0 - t) * (1.0 - t));
    }
    const double t = (p - shape.recoilEnd) / (1.0 - shape.recoilEnd);
    return blend(shape.recoil, 0.0, smooth(t));
}

} // namespace

const Shape &shapeOf(Style style)
{
    return style == Style::Mining ? miningShape : clickShape;
}

Angles at(double progress, Style style)
{
    const double pitch = pitchAt(std::clamp(progress, 0.0, 1.0), shapeOf(style));
    const double reach = std::max(0.0, -pitch) / -shapeOf(style).strike;
    return {pitch, strikeYaw * reach, strikeRoll * reach, reach};
}

} // namespace QcHandSwing

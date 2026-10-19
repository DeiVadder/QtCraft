#include "clientcore/input/QcGamepadMapping.h"

#include <cmath>

namespace QcGamepadMapping {

Walk walk(const QcGamepadState &shaped)
{
    // Pushing the stick up (y negative) walks forward; +0.0f turns a negative zero into zero.
    return {-shaped.axis(QcGamepadAxis::LeftY) + 0.0f, shaped.axis(QcGamepadAxis::LeftX)};
}

QPointF lookDelta(const QcGamepadState &shaped, double lookSpeed, double seconds)
{
    const double degrees = std::isfinite(lookSpeed * seconds) ? lookSpeed * seconds : 0.0;
    return {double(shaped.axis(QcGamepadAxis::RightX)) * degrees, double(shaped.axis(QcGamepadAxis::RightY)) * degrees};
}

bool clearlyDeflected(const QcGamepadState &raw)
{
    const auto beyond = [&raw](QcGamepadAxis x, QcGamepadAxis y) { return std::hypot(raw.axis(x), raw.axis(y)) > leadDeflection; };
    return beyond(QcGamepadAxis::LeftX, QcGamepadAxis::LeftY) || beyond(QcGamepadAxis::RightX, QcGamepadAxis::RightY);
}

bool deflected(const QcGamepadState &shaped)
{
    return walk(shaped).moving() || shaped.axis(QcGamepadAxis::RightX) != 0.0f || shaped.axis(QcGamepadAxis::RightY) != 0.0f;
}

bool triggerHeld(float value, bool wasHeld)
{
    return wasHeld ? value >= releaseThreshold : value > pressThreshold;
}

std::optional<QcGamepadButton> Bindings::button(Action action) const
{
    switch (action) {
    case Action::Jump:
        return jump;
    case Action::Sprint:
        return sprint;
    case Action::Sneak:
        return sneak;
    case Action::HotbarPrevious:
        return hotbarPrevious;
    case Action::HotbarNext:
        return hotbarNext;
    case Action::Pause:
        return pause;
    case Action::Inventory:
        return inventory;
    case Action::Chat:
        return chat;
    case Action::Handbook:
        return handbook;
    case Action::CameraView:
        return cameraView;
    case Action::PlayerList:
        return playerList;
    case Action::Drop:
        return drop;
    case Action::PickBlock:
        return pickBlock;
    case Action::HotbarRadial:
        return hotbarRadial;
    case Action::Mine:
    case Action::Use:
    case Action::Count:
        break;
    }
    return std::nullopt;
}

bool held(Action action, const QcGamepadState &shaped, const Bindings &bindings, bool wasHeld)
{
    if (action == Action::Mine) {
        return triggerHeld(shaped.axis(QcGamepadAxis::RightTrigger), wasHeld);
    }
    if (action == Action::Use) {
        return triggerHeld(shaped.axis(QcGamepadAxis::LeftTrigger), wasHeld);
    }

    const std::optional<QcGamepadButton> button = bindings.button(action);
    return button && shaped.pressed(*button);
}

int hotbarStep(int slot, int steps, int size)
{
    if (size <= 0) {
        return slot;
    }
    const int next = (slot + steps) % size;
    return next < 0 ? next + size : next;
}

} // namespace QcGamepadMapping

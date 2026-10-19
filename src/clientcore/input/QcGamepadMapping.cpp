#include "clientcore/input/QcGamepadMapping.h"

#include <algorithm>
#include <array>
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

bool used(const QcGamepadState &raw)
{
    for (std::size_t button = 0; button < qcGamepadButtonCount; ++button) {
        if (raw.pressed(QcGamepadButton(button))) {
            return true;
        }
    }
    return raw.axis(QcGamepadAxis::LeftTrigger) > pressThreshold || raw.axis(QcGamepadAxis::RightTrigger) > pressThreshold
           || clearlyDeflected(raw);
}

bool deflected(const QcGamepadState &shaped)
{
    return walk(shaped).moving() || shaped.axis(QcGamepadAxis::RightX) != 0.0f || shaped.axis(QcGamepadAxis::RightY) != 0.0f;
}

float releaseFor(float press)
{
    return std::max(press - (pressThreshold - releaseThreshold), 0.05f);
}

bool triggerHeld(float value, bool wasHeld, float press)
{
    return wasHeld ? value >= releaseFor(press) : value > press;
}

namespace {

using ButtonField = QcGamepadButton Bindings::*;

// An action of the table with its id and its field; the triggers' actions have none.
struct ActionEntry
{
    Action action = Action::Jump;
    QStringView name;
    ButtonField field = nullptr;
};

constexpr std::array<ActionEntry, actionCount> actionTable{{
    {Action::Jump, u"jump", &Bindings::jump},
    {Action::Sprint, u"sprint", &Bindings::sprint},
    {Action::Sneak, u"sneak", &Bindings::sneak},
    {Action::Mine, u"mine", nullptr},
    {Action::Use, u"use", nullptr},
    {Action::HotbarPrevious, u"hotbarPrevious", &Bindings::hotbarPrevious},
    {Action::HotbarNext, u"hotbarNext", &Bindings::hotbarNext},
    {Action::Pause, u"pause", &Bindings::pause},
    {Action::Inventory, u"inventory", &Bindings::inventory},
    {Action::Chat, u"chat", &Bindings::chat},
    {Action::Handbook, u"handbook", &Bindings::handbook},
    {Action::CameraView, u"cameraView", &Bindings::cameraView},
    {Action::PlayerList, u"playerList", &Bindings::playerList},
    {Action::Drop, u"drop", &Bindings::drop},
    {Action::PickBlock, u"pickBlock", &Bindings::pickBlock},
    {Action::HotbarRadial, u"hotbarRadial", &Bindings::hotbarRadial},
    {Action::HideHud, u"hideHud", &Bindings::hideHud},
    {Action::Screenshot, u"screenshot", &Bindings::screenshot},
}};
static_assert(
    [] {
        for (std::size_t index = 0; index < actionTable.size(); ++index) {
            if (actionTable[index].action != Action(index)) {
                return false;
            }
        }
        return true;
    }(),
    "actionTable follows the order of Action");

ButtonField fieldOf(Action action)
{
    return action < Action::Count ? actionTable[std::size_t(action)].field : nullptr;
}

} // namespace

std::optional<QcGamepadButton> Bindings::button(Action action) const
{
    const ButtonField field = fieldOf(action);
    return field && this->*field != unbound ? std::optional(this->*field) : std::nullopt;
}

std::optional<Action> Bindings::action(QcGamepadButton button) const
{
    for (const ActionEntry &entry : actionTable) {
        if (entry.field && this->*entry.field == button) {
            return entry.action;
        }
    }
    return std::nullopt;
}

void Bindings::setButton(Action action, QcGamepadButton button)
{
    if (const ButtonField field = fieldOf(action); field && (bindable(button) || button == unbound)) {
        this->*field = button;
    }
}

std::optional<Action> Bindings::bind(Action action, QcGamepadButton button)
{
    const ButtonField field = fieldOf(action);
    if (!field || !bindable(button) || stranded(action, button)) {
        return std::nullopt;
    }

    const QcGamepadButton old = this->*field;
    std::optional<Action> displaced;
    for (const ActionEntry &entry : actionTable) {
        if (entry.field && entry.action != action && this->*entry.field == button) {
            this->*entry.field = old;
            displaced = entry.action;
        }
    }
    this->*field = button;
    return displaced;
}

std::optional<Combo> comboOf(Action action)
{
    for (const Combo &combo : combos) {
        if (combo.action == action) {
            return combo;
        }
    }
    return std::nullopt;
}

std::array<bool, actionCount> withCombos(std::array<bool, actionCount> now, const std::array<bool, actionCount> &before,
                                         const QcGamepadState &shaped, const Bindings &bindings)
{
    for (const Combo &combo : combos) {
        const std::optional<QcGamepadButton> modifier = bindings.button(combo.modifier);
        if (!modifier || *modifier == combo.button || !shaped.pressed(combo.button)) {
            continue;
        }

        // A press keeps what it began as: the combo once it started with the modifier held, the
        // button's own action when that was held before the modifier came.
        const std::optional<Action> own = bindings.action(combo.button);
        const bool ownBefore = own && *own != combo.action && before[std::size_t(*own)];
        const bool comboBefore = before[std::size_t(combo.action)];
        if (!comboBefore && (ownBefore || !now[std::size_t(combo.modifier)])) {
            continue;
        }

        now[std::size_t(combo.action)] = true;
        if (own && *own != combo.action && *own != combo.modifier) {
            now[std::size_t(*own)] = false;
        }
    }

    return now;
}

std::optional<Action> Bindings::stranded(Action action, QcGamepadButton button) const
{
    const ButtonField field = fieldOf(action);
    if (!field || this->*field != unbound) {
        return std::nullopt;
    }

    static const Bindings defaults;
    for (const ActionEntry &entry : actionTable) {
        if (entry.field && entry.action != action && this->*entry.field == button && defaults.*entry.field != unbound) {
            return entry.action;
        }
    }
    return std::nullopt;
}

QList<Action> Bindings::conflicts() const
{
    QList<Action> result;
    std::array<bool, qcGamepadButtonCount> used{};
    for (const ActionEntry &entry : actionTable) {
        if (!entry.field || this->*entry.field == unbound) {
            continue;
        }
        bool &seen = used[std::size_t(this->*entry.field)];
        if (seen) {
            result.append(entry.action);
        }
        seen = true;
    }
    return result;
}

QString actionName(Action action)
{
    return action < Action::Count ? actionTable[std::size_t(action)].name.toString() : QString();
}

std::optional<Action> actionFromName(const QString &name)
{
    for (const ActionEntry &entry : actionTable) {
        if (entry.name == name) {
            return entry.action;
        }
    }
    return std::nullopt;
}

bool rebindable(Action action)
{
    return fieldOf(action) != nullptr;
}

bool bindable(QcGamepadButton button)
{
    return button != QcGamepadButton::Guide && button < QcGamepadButton::Count;
}

bool held(Action action, const QcGamepadState &shaped, const Bindings &bindings, bool wasHeld, float press)
{
    if (action == Action::Mine) {
        return triggerHeld(shaped.axis(QcGamepadAxis::RightTrigger), wasHeld, press);
    }
    if (action == Action::Use) {
        return triggerHeld(shaped.axis(QcGamepadAxis::LeftTrigger), wasHeld, press);
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

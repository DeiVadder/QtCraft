#include "clientcore/input/QcVirtualGamepadSource.h"

using namespace Qt::StringLiterals;

QString QcVirtualGamepadSource::backendName() const
{
    return u"virtual"_s;
}

int QcVirtualGamepadSource::connectPad(const QString &name, QcGamepadType type, const QString &guid)
{
    return addPad({.name = name, .type = type, .guid = guid, .rumble = true});
}

bool QcVirtualGamepadSource::disconnectPad(int id)
{
    if (!contains(id)) {
        return false;
    }

    m_rumbles.remove(id);
    removePad(id);
    return true;
}

bool QcVirtualGamepadSource::rumble(int id, float strength, int durationMs)
{
    if (!contains(id)) {
        return false;
    }

    m_rumbles[id].append({strength, durationMs});
    return true;
}

bool QcVirtualGamepadSource::setState(int id, const QcGamepadState &state)
{
    if (!contains(id)) {
        return false;
    }

    updateState(id, state);
    return true;
}

bool QcVirtualGamepadSource::setPressed(int id, QcGamepadButton button, bool pressed)
{
    QcGamepadState next = state(id);
    next.setPressed(button, pressed);
    return setState(id, next);
}

bool QcVirtualGamepadSource::setAxis(int id, QcGamepadAxis axis, float value)
{
    QcGamepadState next = state(id);
    next.setAxis(axis, value);
    return setState(id, next);
}

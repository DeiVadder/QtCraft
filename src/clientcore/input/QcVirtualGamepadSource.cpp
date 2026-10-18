#include "clientcore/input/QcVirtualGamepadSource.h"

using namespace Qt::StringLiterals;

QString QcVirtualGamepadSource::backendName() const
{
    return u"virtual"_s;
}

int QcVirtualGamepadSource::connectPad(const QString &name, QcGamepadType type)
{
    return addPad(name, type);
}

bool QcVirtualGamepadSource::disconnectPad(int id)
{
    if (!contains(id)) {
        return false;
    }

    removePad(id);
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

#include "clientcore/input/QcGamepadSource.h"

QcGamepadSource::QcGamepadSource(QObject *parent)
    : QObject(parent)
{
}

QcGamepadSource::~QcGamepadSource() = default;

QList<QcGamepadInfo> QcGamepadSource::pads() const
{
    QList<QcGamepadInfo> result;
    result.reserve(m_pads.size());
    for (const Pad &pad : m_pads) {
        result.append(pad.info);
    }
    return result;
}

QcGamepadState QcGamepadSource::state(int id) const
{
    const auto it = m_pads.constFind(id);
    return it == m_pads.constEnd() ? QcGamepadState{} : it->state;
}

int QcGamepadSource::addPad(const QString &name, QcGamepadType type)
{
    const int id = m_nextId++;
    m_pads.insert(id, Pad{.info = {.id = id, .name = name, .type = type}, .state = {}});
    emit padConnected(id);
    return id;
}

void QcGamepadSource::removePad(int id)
{
    if (m_pads.remove(id) > 0) {
        emit padDisconnected(id);
    }
}

void QcGamepadSource::updateState(int id, const QcGamepadState &state)
{
    const auto it = m_pads.find(id);
    if (it == m_pads.end() || it->state == state) {
        return;
    }

    it->state = state;
    emit stateChanged(id);
}

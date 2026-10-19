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

std::optional<QcGamepadInfo> QcGamepadSource::info(int id) const
{
    const auto it = m_pads.constFind(id);
    return it == m_pads.constEnd() ? std::nullopt : std::optional(it->info);
}

bool QcGamepadSource::rumble(int id, float strength, int durationMs)
{
    Q_UNUSED(id);
    Q_UNUSED(strength);
    Q_UNUSED(durationMs);
    return false;
}

int QcGamepadSource::addPad(QcGamepadInfo info)
{
    const int id = m_nextId++;
    info.id = id;
    m_pads.insert(id, Pad{.info = std::move(info), .state = {}});
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

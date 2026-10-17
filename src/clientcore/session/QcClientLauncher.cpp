#include "clientcore/session/QcClientLauncher.h"

#include "common/physics/QcPhysics.h"

#include <algorithm>
#include <cmath>

bool QcClientLauncher::press(const QcItemDefinition &held, const QcInventory &inventory, const QcContentRegistry &content, bool creative)
{
    if (m_launcher || !held.launcher || (!creative && inventory.ammunitionSlot(content, *held.launcher) < 0)) {
        return false;
    }
    m_launcher = held.launcher;
    m_slot = inventory.selectedHotbarSlot();
    m_ticks = 0;
    return true;
}

bool QcClientLauncher::release()
{
    const bool was = m_launcher.has_value();
    m_launcher.reset();
    return was;
}

std::optional<QcMessage> QcClientLauncher::use(QcItemId item, const QcItemDefinition &definition, const QcInventory &inventory,
                                                const QcContentRegistry &content, bool creative)
{
    if (definition.throwable) {
        return mayThrow(item, *definition.throwable) ? std::optional<QcMessage>(QcMsg::ThrowItem{}) : std::nullopt;
    }
    if (press(definition, inventory, content, creative)) {
        return QcMsg::DrawLauncher{QcMsg::DrawLauncher::Action::Draw};
    }
    return std::nullopt;
}

bool QcClientLauncher::mayThrow(QcItemId item, const QcThrowableDefinition &throwable)
{
    // A small margin: the counted server ticks are a sum of fractions.
    if (m_clock + 1e-6 < m_throwReady.value(item, 0.0)) {
        return false;
    }
    m_throwReady.insert(item, m_clock + double(std::lround(throwable.cooldownSeconds / QcPhysics::tickDuration)));
    return true;
}

void QcClientLauncher::tick(const QcItemDefinition *held, int slot, double serverTicks)
{
    m_clock += std::max(0.0, serverTicks);
    if (!m_launcher) {
        return;
    }
    if (!held || held->launcher != m_launcher || slot != m_slot) {
        m_launcher.reset();
        return;
    }
    ++m_ticks;
}

double QcClientLauncher::progress(double alpha) const
{
    return m_launcher ? m_launcher->powerAfter((m_ticks + alpha) * QcPhysics::tickDuration) : -1.0;
}

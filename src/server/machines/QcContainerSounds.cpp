// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/machines/QcContainerSounds.h"

#include "common/gameplay/QcPortableContainers.h"
#include "common/physics/QcPhysics.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"

namespace {

QcVec3 centreOf(QcBlockPos pos)
{
    return {pos.x + 0.5, pos.y + 0.5, pos.z + 0.5};
}

} // namespace

QcContainerSounds::QcContainerSounds(QcServerState &state, QcServerSounds &sounds)
    : m_state(state)
    , m_sounds(sounds)
{
}

void QcContainerSounds::update()
{
    Open now = openNow();

    for (auto it = now.placed.cbegin(); it != now.placed.cend(); ++it) {
        if (!m_open.placed.contains(it.key())) {
            play(it.value(), true, centreOf(it.key()));
        }
    }
    for (auto it = m_open.placed.cbegin(); it != m_open.placed.cend(); ++it) {
        // A container broken while open makes its breaking sound instead. The comparison is by block
        // type, not state: a trapped chest switches its power state while it is open and closing.
        if (!now.placed.contains(it.key()) && isSameBlockAt(it.key(), it.value())) {
            play(it.value(), false, centreOf(it.key()));
        }
    }

    for (auto it = now.held.cbegin(); it != now.held.cend(); ++it) {
        if (!m_open.held.contains(it.key())) {
            play(it->block, true, it->position);
        }
    }
    for (auto it = m_open.held.cbegin(); it != m_open.held.cend(); ++it) {
        if (!now.held.contains(it.key())) {
            play(it->block, false, it->position);
        }
    }

    m_open = std::move(now);
}

QcContainerSounds::Open QcContainerSounds::openNow() const
{
    Open open;
    for (const auto &client : m_state.clients()) {
        if (client->state != QcServerClient::State::Playing || client->dead) {
            continue;
        }

        if (const std::optional<QcBlockId> held = heldBlockOf(*client)) {
            const QcVec3 eyes = client->motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
            open.held.insert(client->name, {*held, eyes});
            continue;
        }

        const std::optional<QcBlockPos> pos = client->screen.cart == 0 ? client->screen.open : std::nullopt;
        const std::optional<QcBlockId> block = pos ? m_state.world().blockAt(*pos) : std::nullopt;
        if (block && m_state.content().isValidBlock(*block) && m_state.content().block(*block).containerSlots > 0) {
            open.placed.insert(*pos, *block);
        }
    }
    return open;
}

bool QcContainerSounds::isSameBlockAt(QcBlockPos pos, QcBlockId block) const
{
    const std::optional<QcBlockId> now = m_state.world().blockAt(pos);
    return now && m_state.content().isValidBlock(*now) && m_state.content().isSameBlock(*now, block);
}

std::optional<QcBlockId> QcContainerSounds::heldBlockOf(const QcServerClient &client) const
{
    if (!client.screen.held) {
        return std::nullopt;
    }

    const QcContentRegistry &content = m_state.content();
    const QcItemStack &stack = client.inventory.slot(client.screen.held->slot);
    if (stack.isEmpty() || !QcPortableContainers::isPortable(content, stack.item)) {
        return std::nullopt;
    }
    return content.blockId(content.item(stack.item).placesBlock);
}

void QcContainerSounds::play(QcBlockId block, bool opening, const QcVec3 &position)
{
    if (!m_state.content().isValidBlock(block)) {
        return;
    }

    const QcBlockDefinition &definition = m_state.content().block(block);
    const QString &key = opening ? definition.containerOpenSound : definition.containerCloseSound;
    if (!key.isEmpty()) {
        m_sounds.play(key, position);
    }
}

// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcAnvilController.h"

#include "app/debug/QcAppLogging.h"
#include "app/session/QcSessionParts.h"
#include "clientcore/session/QcClientSession.h"
#include "common/gameplay/QcAnvil.h"

QcAnvilController::QcAnvilController(const QcSessionParts &parts, QObject *parent)
    : QObject(parent)
    , m_parts(parts)
{
}

bool QcAnvilController::isOpen() const
{
    return m_parts.session && m_parts.session->anvil().isOpen();
}

int QcAnvilController::maxNameLength()
{
    return static_cast<int>(QcItemData::maxNameLength);
}

void QcAnvilController::rename(const QString &text) const
{
    if (!isOpen()) {
        qCWarning(lcGame) << "Anvil: no open anvil to rename at";
        return;
    }
    m_parts.session->anvil().rename(text);
}

void QcAnvilController::forge() const
{
    if (!isOpen() || !m_hasOutput || !m_affordable) {
        qCWarning(lcGame) << "Anvil: nothing to take (open" << isOpen() << "output" << m_hasOutput << "affordable" << m_affordable << ")";
        return;
    }
    m_parts.session->anvil().forge();
}

void QcAnvilController::update()
{
    const bool open = isOpen();
    const QcMsg::AnvilState state = open ? m_parts.session->anvil().state() : QcMsg::AnvilState{};
    const bool free = open && m_parts.session->gameMode().rules().itemCatalogue;
    const int level = open ? m_parts.session->vitals().experienceLevel() : 0;
    m_name = state.name;
    m_leftName = state.left.name();
    m_hasOutput = !state.output.isEmpty();
    m_levelCost = state.levelCost;
    m_tooExpensive = !free && state.levelCost >= QcAnvil::tooExpensive;
    m_affordable = m_hasOutput && (free || (!m_tooExpensive && level >= state.levelCost));

    // The field follows the left stack (item or data), not the server's echo of the typed name.
    const bool reset = open && (!m_wasOpen || state.left.item != m_left.item || !(state.left.itemData() == m_left.itemData()));
    m_wasOpen = open;
    m_left = state.left;
    emit changed();
    if (reset) {
        ++m_nameResets;
        emit nameResetsChanged();
    }
}

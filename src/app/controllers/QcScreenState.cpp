#include "app/controllers/QcScreenState.h"

#include "app/session/QcSessionParts.h"
#include "clientcore/session/QcClientSession.h"

using namespace Qt::StringLiterals;

QcScreenState::QcScreenState(const QcSessionParts &parts, QObject *parent)
    : QObject(parent)
    , m_parts(parts)
{
}

void QcScreenState::setPhase(Phase phase, const QString &status)
{
    if (m_statusText != status) {
        m_statusText = status;
        emit statusChanged();
    }
    if (m_phase != phase) {
        m_phase = phase;
        emit phaseChanged();
        emit worldInputChanged();
    }
}

void QcScreenState::setInputEnabled(bool enabled)
{
    if (m_inputEnabled == enabled) {
        return;
    }

    m_inputEnabled = enabled;
    if (!enabled) {
        emit interrupted();
        emit mouseFreed();
    }
    emit inputEnabledChanged();
    emit worldInputChanged();
}

void QcScreenState::setOverlay(const QString &overlay)
{
    if (m_overlay == overlay) {
        return;
    }

    m_overlay = overlay;
    emit interrupted();
    if (m_overlay != u"chat"_s) {
        m_chatStart.clear();
    }
    closeSessionScreens();
    emit overlayChanged();
    // Opening a screen frees the mouse; closing the last one captures it again without a click.
    emit worldInputChanged();
}

void QcScreenState::closeSessionScreens() const
{
    if (!m_parts.session) {
        return;
    }

    m_parts.session->setMining(false);
    // A screen opening or closing lets a drawn bow go without a shot.
    m_parts.session->cancelUse();
    // Leaving the inventory hands back the grid contents (and closes a crafting table); leaving
    // the furnace or chest screen closes the block.
    if (m_overlay != u"inventory"_s) {
        m_parts.session->closeCrafting();
    }
    if (m_overlay != u"furnace"_s && m_overlay != u"chest"_s) {
        m_parts.session->closeMachine();
    }
    if (m_overlay != u"trade"_s) {
        m_parts.session->trading().close();
    }
}

void QcScreenState::openChat(const QString &start)
{
    m_chatStart = start;
    setOverlay(u"chat"_s);
}

void QcScreenState::craftingOpened()
{
    setOverlay(u"inventory"_s);
}

void QcScreenState::gameModeChanged(bool creative)
{
    // An operator changed the mode while the creative inventory was open: the usual one instead.
    if (m_overlay == u"creative"_s && !creative) {
        setOverlay(u"inventory"_s);
    }
}

void QcScreenState::machineOpened(bool container)
{
    setOverlay(container ? u"chest"_s : u"furnace"_s);
}

void QcScreenState::machineClosed()
{
    if (m_overlay == u"furnace"_s || m_overlay == u"chest"_s) {
        setOverlay({});
    }
}

void QcScreenState::tradeOpened()
{
    setOverlay(u"trade"_s);
}

void QcScreenState::tradeClosed()
{
    if (m_overlay == u"trade"_s) {
        setOverlay({});
    }
}

void QcScreenState::healthChanged(bool dead)
{
    if (!dead) {
        // Respawned: back into the world with the mouse captured.
        emit worldInputChanged();
        return;
    }

    setOverlay({});
    emit mouseFreed();
    // Releases on the death screen go to QML; forget held keys so nothing sticks after respawn.
    emit interrupted();
    if (m_parts.session) {
        m_parts.session->setMining(false);
        m_parts.session->cancelUse();
    }
}

void QcScreenState::sessionDisconnected(const QString &reason)
{
    if (m_phase != Phase::Menu) {
        // The disconnect screen shows the reason and leads back to the menu.
        emit mouseFreed();
        setPhase(Phase::Disconnected, reason);
    }
}

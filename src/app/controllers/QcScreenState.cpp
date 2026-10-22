#include "app/controllers/QcScreenState.h"

#include "app/debug/QcAppLogging.h"
#include "app/session/QcSessionParts.h"
#include "clientcore/session/QcClientSession.h"

#include <array>

using namespace Qt::StringLiterals;

namespace {

// The command-line names in the order of QcScreenState::Overlay.
constexpr std::array<QStringView, 21> overlayNames{
    u"",
    u"inventory",
    u"creative",
    u"furnace",
    u"chest",
    u"trade",
    u"pause",
    u"settings",
    u"controls",
    u"gamepad",
    u"debug",
    u"texturepacks",
    u"character",
    u"lan",
    u"chat",
    u"sign",
    u"stonecutter",
    u"display",
    u"audio",
    u"interface",
    u"admin",
};
static_assert(overlayNames.size() == std::size_t(QcScreenState::Overlay::Admin) + 1, "a name for every overlay");

} // namespace

QcScreenState::QcScreenState(const QcSessionParts &parts, QObject *parent)
    : QObject(parent)
    , m_parts(parts)
{
    bool valid = false;
    const int keepAliveMs = qEnvironmentVariableIntValue("QTCRAFT_SCREEN_KEEP_ALIVE_MS", &valid);
    if (valid && keepAliveMs >= 0) {
        m_screenKeepAliveMs = keepAliveMs;
    } else if (qEnvironmentVariableIsSet("QTCRAFT_SCREEN_KEEP_ALIVE_MS")) {
        qCWarning(lcGame) << "Ignoring QTCRAFT_SCREEN_KEEP_ALIVE_MS" << qEnvironmentVariable("QTCRAFT_SCREEN_KEEP_ALIVE_MS")
                             << ": expected milliseconds >= 0";
    }
}

QString QcScreenState::overlayName(Overlay overlay)
{
    const auto index = std::size_t(overlay);
    return index < overlayNames.size() ? overlayNames[index].toString() : QString();
}

bool QcScreenState::isMenu(Overlay overlay)
{
    switch (overlay) {
    case Overlay::Pause:
    case Overlay::Settings:
    case Overlay::Display:
    case Overlay::Audio:
    case Overlay::Interface:
    case Overlay::Controls:
    case Overlay::Gamepad:
    case Overlay::Debug:
    case Overlay::TexturePacks:
    case Overlay::Character:
    case Overlay::Lan:
    case Overlay::Admin:
        return true;
    default:
        return false;
    }
}

std::optional<QcScreenState::Overlay> QcScreenState::overlayFromName(QStringView name)
{
    for (std::size_t index = 0; index < overlayNames.size(); ++index) {
        if (overlayNames[index] == name) {
            return Overlay(index);
        }
    }
    return std::nullopt;
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

void QcScreenState::setOverlay(Overlay overlay)
{
    if (overlayNames.size() <= std::size_t(overlay)) {
        qCWarning(lcGame) << "Ignoring unknown overlay" << int(overlay);
        return;
    }
    // The same screen again changes nothing (screens set it on every click of their buttons).
    if (m_overlay == overlay) {
        return;
    }

    m_overlay = overlay;
    emit interrupted();
    if (m_overlay != Overlay::Chat) {
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
    if (m_overlay != Overlay::Inventory) {
        m_parts.session->inventoryMirror().closeCrafting();
    }
    if (m_overlay != Overlay::Furnace && m_overlay != Overlay::Chest) {
        m_parts.session->closeMachine();
    }
    if (m_overlay != Overlay::Trade) {
        m_parts.session->trading().close();
    }
    if (m_overlay != Overlay::Stonecutter) {
        m_parts.session->stonecutter().close();
    }
    // A sign editor closed another way than by its Done button keeps the sign's text.
    QcClientSignEditor &sign = m_parts.session->signEditor();
    if (m_overlay != Overlay::Sign && sign.isOpen()) {
        sign.write(sign.lines());
    }
}

void QcScreenState::openChat(const QString &start)
{
    m_chatStart = start;
    setOverlay(Overlay::Chat);
}

void QcScreenState::craftingOpened()
{
    setOverlay(Overlay::Inventory);
}

void QcScreenState::gameModeChanged(QcGameModes::Rules rules)
{
    // A spectator uses no screen of items (decision E-730): it closes, which tells the server.
    const bool itemScreen = m_overlay == Overlay::Inventory || m_overlay == Overlay::Creative || m_overlay == Overlay::Furnace
                            || m_overlay == Overlay::Chest || m_overlay == Overlay::Trade || m_overlay == Overlay::Stonecutter;
    if (itemScreen && !rules.interacts) {
        setOverlay(Overlay::None);
        return;
    }

    // An operator changed the mode while the creative inventory was open: the usual one instead.
    if (m_overlay == Overlay::Creative && !rules.itemCatalogue) {
        setOverlay(Overlay::Inventory);
    }
}

void QcScreenState::machineOpened(bool container)
{
    setOverlay(container ? Overlay::Chest : Overlay::Furnace);
}

void QcScreenState::machineClosed()
{
    if (m_overlay == Overlay::Furnace || m_overlay == Overlay::Chest) {
        setOverlay(Overlay::None);
    }
}

void QcScreenState::tradeOpened()
{
    setOverlay(Overlay::Trade);
}

void QcScreenState::tradeClosed()
{
    if (m_overlay == Overlay::Trade) {
        setOverlay(Overlay::None);
    }
}

void QcScreenState::signOpened()
{
    setOverlay(Overlay::Sign);
}

void QcScreenState::stonecutterOpened()
{
    setOverlay(Overlay::Stonecutter);
}

void QcScreenState::stonecutterClosed()
{
    if (m_overlay == Overlay::Stonecutter) {
        setOverlay(Overlay::None);
    }
}

void QcScreenState::healthChanged(bool dead)
{
    if (!dead) {
        // Respawned: back into the world with the mouse captured.
        emit worldInputChanged();
        return;
    }

    setOverlay(Overlay::None);
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

#include "server/tick/QcServerPause.h"

#include "server/core/QcServerClient.h"

#include <algorithm>

std::optional<QcServerPause::Reason> QcServerPause::update(const std::vector<std::unique_ptr<QcServerClient>> &clients, qint64 playerTimeNs,
                                                           const ViewCheck &viewComplete)
{
    const Reason reason = decide(clients, playerTimeNs, viewComplete);
    if (reason == m_reason) {
        return std::nullopt;
    }

    m_reason = reason;
    return reason;
}

QcServerPause::Reason QcServerPause::decide(const std::vector<std::unique_ptr<QcServerClient>> &clients, qint64 playerTimeNs,
                                            const ViewCheck &viewComplete)
{
    const auto open = [](const std::unique_ptr<QcServerClient> &client) { return client->state != QcServerClient::State::Closed; };
    if (std::ranges::none_of(clients, open)) {
        if (m_emptyNs <= 0) {
            return Reason::None;
        }
        // The players' time stands still while paused, so the reason holds until someone connects.
        if (!m_emptySinceNs) {
            m_emptySinceNs = playerTimeNs;
        }
        return playerTimeNs - *m_emptySinceNs >= m_emptyNs ? Reason::Empty : Reason::None;
    }

    m_emptySinceNs.reset();
    // A client still logging in counts as someone else: it keeps the world running.
    const bool menu = std::ranges::all_of(clients, [&open, &viewComplete](const std::unique_ptr<QcServerClient> &client) {
        return !open(client)
               || (client->local && client->pauseRequested && client->state == QcServerClient::State::Playing && viewComplete(*client));
    });
    return menu ? Reason::Menu : Reason::None;
}

const char *QcServerPause::name(Reason reason)
{
    switch (reason) {
    case Reason::Menu:
        return "pause menu";
    case Reason::Empty:
        return "no players";
    case Reason::None:
        break;
    }
    return "running";
}

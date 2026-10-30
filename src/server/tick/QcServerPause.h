// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

struct QcServerClient;

/*! \brief Whether the server's tick loop pauses (decision E-877).

    It pauses for the pause menu of a world hosted in the game (every connected client is local,
    asked for it, QcMsg::PauseRequest, and has its view: a world opened straight into the pause
    menu still loads around the player) and when no one was connected for \c {emptySeconds} of
    the players' time (a dedicated server; 0 never). A network client's wish never pauses a
    shared server, and anyone connecting resumes it at once, so the login runs. While paused the
    server runs no tick: the tick counter, the time of day and the simulation stand still;
    messages, the console, the admin channel and logins go on between ticks. */
class QcServerPause
{
public:
    /*! Why the server pauses. */
    enum class Reason {
        /*! It does not pause. */
        None,
        /*! The pause menu of a world hosted in the game. */
        Menu,
        /*! No one was connected for the configured time. */
        Empty
    };

    explicit QcServerPause(int emptySeconds)
        : m_emptyNs(static_cast<qint64>(emptySeconds) * 1'000'000'000)
    {
    }

    /*! Returns why the server pauses now. */
    Reason reason() const { return m_reason; }
    /*! Returns whether the server pauses now. */
    bool paused() const { return m_reason != Reason::None; }
    /*! Whether a client has every chunk of its view (QcChunkStreaming::viewComplete). */
    using ViewCheck = std::function<bool(const QcServerClient &)>;

    /*! Decides anew from \a clients (closed ones count as gone) at \a playerTimeNs, with
        \a viewComplete for the pause menu. Returns the new reason when it changed, else
        std::nullopt. */
    [[nodiscard]] std::optional<Reason> update(const std::vector<std::unique_ptr<QcServerClient>> &clients, qint64 playerTimeNs,
                                               const ViewCheck &viewComplete);
    /*! Something may have changed the world while paused (a console command): takeChange() is
        true once, so the paused server saves it. */
    void noteChange() { m_changed = paused(); }
    /*! Returns whether something changed the world while paused since the last call. */
    [[nodiscard]] bool takeChange() { return std::exchange(m_changed, false); }
    /*! Returns \a reason as the log names it ("pause menu", "no players"). */
    static const char *name(Reason reason);

private:
    [[nodiscard]] Reason decide(const std::vector<std::unique_ptr<QcServerClient>> &clients, qint64 playerTimeNs, const ViewCheck &viewComplete);

    qint64 m_emptyNs = 0;
    // The players' time since which no one is connected.
    std::optional<qint64> m_emptySinceNs;
    Reason m_reason = Reason::None;
    bool m_changed = false;
};

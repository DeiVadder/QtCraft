// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

#include <optional>

/*! \brief What the game does when the graphics device is lost (decision E-963): a lost device
    while the realistic mode is drawn gives coolDownMs() of classic frames, a second one within
    repeatWindowMs of the first draws the classic look for the rest of the session.

    A weak GPU whose driver resets after a too long submission (Windows' TDR) would otherwise fall
    straight back into the same heavy frames on the new device. A loss while the classic look is
    drawn is not the realistic mode's fault and changes nothing. Pure logic: the caller passes the
    time in milliseconds of a monotonic clock. */
class QcDeviceLossPolicy
{
public:
    /*! \brief The answer to a lost device: Recover draws on as chosen, CoolDown draws classic
        frames for coolDownMs(), FallBack draws the classic look until the player chooses the render
        mode again. */
    enum class Response {
        Recover,  // nothing changes
        CoolDown, // classic frames for a while
        FallBack  // the classic look for the session
    };

    /*! Classic frames after the first loss in the realistic mode, unless set otherwise. */
    static constexpr qint64 defaultCoolDownMs = 20'000;
    /*! A second loss in the realistic mode within this time of the previous one falls back. */
    static constexpr qint64 repeatWindowMs = 60'000;

    /*! Makes a policy with \a coolDownMs of classic frames after a first loss (tests take a short
        one). */
    explicit QcDeviceLossPolicy(qint64 coolDownMs = defaultCoolDownMs);

    /*! Returns the classic frames after a first loss in milliseconds. */
    [[nodiscard]] qint64 coolDownMs() const { return m_coolDownMs; }
    /*! Returns the answer to a device lost at \a nowMs while the realistic mode was drawn
        (\a realistic) or not. A realistic loss during a cool-down (the game draws the classic
        look then, so it does not happen there) counts like any other: within repeatWindowMs of
        the previous one it falls back. A classic loss changes nothing, neither the cool-down nor
        the window of the previous realistic loss. */
    Response deviceLost(qint64 nowMs, bool realistic);
    /*! Returns whether the realistic mode may be drawn at \a nowMs. */
    [[nodiscard]] bool realisticAllowed(qint64 nowMs) const;
    /*! Returns the milliseconds of classic frames left at \a nowMs, 0 when none are left or the
        policy fell back for the session. */
    [[nodiscard]] qint64 coolDownLeftMs(qint64 nowMs) const;
    /*! Returns whether the classic look is drawn for the rest of the session. */
    [[nodiscard]] bool fellBack() const { return m_fellBack; }
    /*! The player chose the render mode again: the realistic mode may be drawn and the losses
        before count no more. */
    void choseAgain();

private:
    void setFellBack(bool fellBack) { m_fellBack = fellBack; }
    [[nodiscard]] qint64 coolDownEndMs() const { return m_coolDownEndMs; }
    void setCoolDownEndMs(qint64 ms) { m_coolDownEndMs = ms; }
    [[nodiscard]] std::optional<qint64> lastRealisticLossMs() const { return m_lastRealisticLossMs; }
    void setLastRealisticLossMs(qint64 ms) { m_lastRealisticLossMs = ms; }

    qint64 m_coolDownMs;
    std::optional<qint64> m_lastRealisticLossMs;
    qint64 m_coolDownEndMs = 0;
    bool m_fellBack = false;
};

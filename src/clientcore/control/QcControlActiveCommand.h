// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonObject>

#include <functional>

/*! \brief A control request that completes on a later tick. */
struct QcControlActiveCommand
{
    /*! The request the command answers. */
    QJsonObject request;
    /*! Poll intervals left before the command fails with a timeout. */
    int remainingTicks = 0;
    /*! Called every tick; returns true when the command is complete. */
    std::function<bool()> step;
    /*! Response of a completed command; the player status when unset. The initializer lets
        commands leave it out without -Wmissing-field-initializers. */
    std::function<QJsonObject()> result = {}; // NOLINT(readability-redundant-member-init)

    /*! Returns the poll intervals a command of \a ticks ticks gets: tick-based commands fail when
        the session stops ticking (e.g. while loading) for much longer than asked. */
    static constexpr int timeoutPolls(int ticks) { return ticks * 4 + 100; }
};

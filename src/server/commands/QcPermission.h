// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

/*! \brief What a command source may do (decision E-243).

    Every player may use the player commands; operators (the operator list of the world, the
    local single player) all others except the server's own; the server console may use
    everything. */
enum class QcPermission : quint8 {
    /*! Every player. */
    Player,
    /*! An operator. */
    Operator,
    /*! The server console. */
    Console,
};

/*! Returns whether a source with the right \a granted may run what needs \a needed. */
inline bool qcAllows(QcPermission granted, QcPermission needed)
{
    return static_cast<quint8>(granted) >= static_cast<quint8>(needed);
}

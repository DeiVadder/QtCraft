// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockFace.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"

#include <optional>

class QcClientSession;

/*! \brief Turns the player's view like a player moving the mouse. */
class QcControlAim
{
public:
    /*! Turns the player of \a session towards the centre of \a block, or towards the centre of
        its \a face. */
    static void lookAt(QcClientSession &session, QcBlockPos block, std::optional<QcBlockFace> face);
    /*! Turns the player of \a session towards \a point. */
    static void lookAtPoint(QcClientSession &session, const QcVec3 &point);
};

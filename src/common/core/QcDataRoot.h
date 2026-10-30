// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

/*! \brief The folder of the player's data (worlds, mods, the player key, server settings):
    QTCRAFT_DATA_DIR when set, else the application data location of the platform. */
namespace QcDataRoot {

/*! Returns the folder's path. */
QString path();

} // namespace QcDataRoot

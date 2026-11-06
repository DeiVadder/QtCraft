// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/textures/QcTextureRelief.h"

#include <span>

/*! Returns the surfaces of the laid stone: ashlar and fired bricks, each plain,
    cracked and carved (decision E-1043). */
[[nodiscard]] std::span<const QcTextureRelief::Entry> qcMasonrySurfaces();

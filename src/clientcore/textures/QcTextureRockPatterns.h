// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/textures/QcTextureRelief.h"

#include <span>

/*! Returns the surfaces of the materials of the Nether's ground and its glowing
    blocks: fused rock, hollow sand, crusted soil, magma, glowing clusters and the ores in fused
    rock (decision E-1043). */
[[nodiscard]] std::span<const QcTextureRelief::Entry> qcRockSurfaces();

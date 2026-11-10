// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/textures/QcTextureRelief.h"

#include <span>

/*! Returns the relief patterns of the nether forests (decision E-1048): fungal mats, stems,
    boards, wart flesh and glowing lobes. */
[[nodiscard]] std::span<const QcTextureRelief::Entry> qcFungalSurfaces();

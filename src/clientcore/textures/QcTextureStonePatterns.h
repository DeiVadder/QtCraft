// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/textures/QcTextureRelief.h"

#include <span>

/*! Returns the surfaces of the columnar and glassy volcanic stones: basalt
    columns, their polished forms, flow bands, glassy stone, its polished and gilded forms
    (decision E-1043). */
[[nodiscard]] std::span<const QcTextureRelief::Entry> qcStoneSurfaces();

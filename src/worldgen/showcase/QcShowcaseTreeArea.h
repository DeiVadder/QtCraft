// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/showcase/QcShowcaseArea.h"

// Every tree and column feature of the registry, grown as the world generator grows it, and in
// front of each the saplings that grow into it, planted and growing (decision E-220).
namespace QcShowcaseTreeArea {

QcShowcaseArea build(const QcShowcaseContext &context, QcBlockPos anchor);

} // namespace QcShowcaseTreeArea

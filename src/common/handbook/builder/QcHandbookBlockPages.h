// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/handbook/QcHandbookEntry.h"

#include <QList>

struct QcHandbookContext;

// Handbook pages of the visible blocks (categories "blocks", "parts" and "liquids"), in content
// order: how they are mined, their physical behaviour, uses, recipes and where they occur.
namespace QcHandbookBlockPages {

QList<QcHandbookEntry> build(const QcHandbookContext &context);

} // namespace QcHandbookBlockPages

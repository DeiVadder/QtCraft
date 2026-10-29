// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/handbook/QcHandbookEntry.h"

#include <QList>

struct QcHandbookContext;

// Handbook pages of the items without a block page (categories "items", "tools" and "food"), in
// content order.
namespace QcHandbookItemPages {

QList<QcHandbookEntry> build(const QcHandbookContext &context);

} // namespace QcHandbookItemPages

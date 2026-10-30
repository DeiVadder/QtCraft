// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/handbook/QcHandbookEntry.h"

#include <QList>

struct QcHandbookContext;

/*! \brief Handbook pages of the creatures (category "creatures"), in content order: behaviour,
    spawning and drops. */
namespace QcHandbookCreaturePages {

/*! Returns the pages, built with the shared parts of \a context. */
QList<QcHandbookEntry> build(const QcHandbookContext &context);

} // namespace QcHandbookCreaturePages

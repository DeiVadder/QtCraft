// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QLoggingCategory>

// The logging categories of qc_worldgen, each defined once in QcWorldgenLogging.cpp (decision E-680).
// Users filter them by name, e.g. QT_LOGGING_RULES="qtcraft.server.debug=true".

// "qtcraft.worldgen": world generation, structure templates and the showcase plan.
Q_DECLARE_LOGGING_CATEGORY(lcWorldgen)

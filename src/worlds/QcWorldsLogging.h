// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QLoggingCategory>

// The logging categories of qc_worlds, each defined once in QcWorldsLogging.cpp (decision E-680).
// Users filter them by name, e.g. QT_LOGGING_RULES="qtcraft.worlds.debug=true". Mod loading and
// content fingerprints log in "qtcraft.content" of qc_server (server/core/QcServerLogging.h), the
// lowest library that uses that category.

// "qtcraft.worlds": the local server host and the worlds on disk.
Q_DECLARE_LOGGING_CATEGORY(lcWorlds)

// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QLoggingCategory>

// The logging categories of qc_clientcore, each defined once in QcClientLogging.cpp (decision E-680).
// Users filter them by name, e.g. QT_LOGGING_RULES="qtcraft.server.debug=true".

// "qtcraft.materials": the material maps of the realistic mode (decision E-908): rejected values.
Q_DECLARE_LOGGING_CATEGORY(lcMaterials)
// "qtcraft.session": the client session: joining, chunks, prediction.
Q_DECLARE_LOGGING_CATEGORY(lcSession)

// "qtcraft.settings": loading and saving settings, also those of the graphical client.
Q_DECLARE_LOGGING_CATEGORY(lcSettings)

// "qtcraft.stats": the statistics log.
Q_DECLARE_LOGGING_CATEGORY(lcStats)

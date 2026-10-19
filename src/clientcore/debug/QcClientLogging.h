#pragma once

#include <QLoggingCategory>

// The logging categories of qc_clientcore, each defined once in QcClientLogging.cpp (decision E-680).
// Users filter them by name, e.g. QT_LOGGING_RULES="qtcraft.server.debug=true".

// "qtcraft.session": the client session: joining, chunks, prediction.
Q_DECLARE_LOGGING_CATEGORY(lcSession)

// "qtcraft.settings": loading and saving settings, also those of the graphical client.
Q_DECLARE_LOGGING_CATEGORY(lcSettings)

// "qtcraft.stats": the statistics log.
Q_DECLARE_LOGGING_CATEGORY(lcStats)

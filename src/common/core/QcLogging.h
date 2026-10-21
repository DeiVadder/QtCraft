#pragma once

#include <QLoggingCategory>

// The logging categories of qc_common, each defined once in QcLogging.cpp (decision E-680).
// Users filter them by name, e.g. QT_LOGGING_RULES="qtcraft.server.debug=true".

// "qtcraft.connection": sending and receiving protocol frames.
Q_DECLARE_LOGGING_CATEGORY(lcConnection)
// "qtcraft.registry": changes to the content registry that it refuses.
Q_DECLARE_LOGGING_CATEGORY(lcRegistry)
// "qtcraft.itemdata": item data refused for going beyond its limits (QcItemStack::withData).
Q_DECLARE_LOGGING_CATEGORY(lcItemData)
// "qtcraft.diagnostics": the log file, crash reports and diagnostics bundles (decision E-834).
Q_DECLARE_LOGGING_CATEGORY(lcDiagnostics)
// "qtcraft.health": the periodic health line of game and server (decision E-834).
Q_DECLARE_LOGGING_CATEGORY(lcHealth)

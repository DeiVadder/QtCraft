#pragma once

#include <QLoggingCategory>

// The logging categories of qc_common, each defined once in QcLogging.cpp (decision E-680).
// Users filter them by name, e.g. QT_LOGGING_RULES="qtcraft.server.debug=true".

// "qtcraft.connection": sending and receiving protocol frames.
Q_DECLARE_LOGGING_CATEGORY(lcConnection)

#pragma once

#include <QLoggingCategory>

// The logging categories of qc_audio, each defined once in QcAudioLogging.cpp (decision E-680).
// Users filter them by name, e.g. QT_LOGGING_RULES="qtcraft.server.debug=true".

// "qtcraft.audio": sound output and the sound bank.
Q_DECLARE_LOGGING_CATEGORY(lcAudio)

#pragma once

#include "QcResult.h"

#include <QString>
#include <QStringList>

class QcContentRegistry;

// Development aid: writes every sound of the content (first variant) and one music piece as 16-bit
// mono WAV files, so that sound definitions can be listened to and checked without the game.
namespace QcSoundExporter {

// Returns one line per file: name, length in seconds and peak level.
QcResult<QStringList> exportAll(const QcContentRegistry &content, const QString &directory, quint64 musicSeed);

} // namespace QcSoundExporter

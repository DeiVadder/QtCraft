#pragma once

#include "QcResult.h"

#include <QString>
#include <QStringList>

// Example mods shipped inside the app (the release bundle's Contents/Resources/mods). On the first
// start, when the player's mods directory does not exist yet, they are copied there switched off,
// so the Mods screen offers them; mods still load only from the player's mods directory.
namespace QcBundledMods {

// Upper bounds for a bundled mods directory (it comes with the app, the limits only keep a broken
// installation from copying without end).
inline constexpr int maxFiles = 4096;
inline constexpr int maxDepth = 8;

// The bundled mods next to the program in `applicationDirectory`: ../Resources/mods (macOS
// bundle), else mods; empty when neither exists.
QString directory(const QString &applicationDirectory);

// Copies every mod directory (with a pack.json) of `source` into `modsDirectory` and disables each
// one there, but only when `source` holds a mod and `modsDirectory` does not exist yet. The copy is
// made in a new folder beside `modsDirectory` (`<name>.installing-XXXXXX`) and renamed when
// complete; on failure only that folder is removed. Linked mod folders and manifests, other links
// and special files are skipped.
// Returns the ids of the installed mods; an empty list when nothing was to be done.
QcResult<QStringList> installDisabled(const QString &source, const QString &modsDirectory);

} // namespace QcBundledMods

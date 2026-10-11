#pragma once

#include <QString>

// The folder of the player's data (worlds, mods, the player key, server settings):
// QTCRAFT_DATA_DIR when set, else the application data location of the platform.
namespace QcDataRoot {

QString path();

} // namespace QcDataRoot

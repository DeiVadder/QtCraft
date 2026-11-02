// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

class QObject;
class QString;

// Menu choices preselected by test hooks, so that screenshots show the fields of a choice.
namespace QcMenuPreselect {

// QTCRAFT_MENU_WORLD_TYPE=flat|void|showcase and QTCRAFT_MENU_GAME_MODE=creative (or another mode):
// the new-world page below `root` (or `root` itself) chooses `worldType` and `gameMode` and starts
// with them whenever it opens (NewWorldPage.startWorldType, startGameMode). Empty leaves a choice
// alone; an unknown name or a value the page does not offer is refused with a warning.
void newWorld(QObject &root, const QString &worldType, const QString &gameMode);

} // namespace QcMenuPreselect

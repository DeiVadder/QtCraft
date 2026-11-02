// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

class QcNewWorldDraft;
class QString;

// Menu choices preselected by test hooks, so that screenshots show the fields of a choice.
namespace QcMenuPreselect {

// QTCRAFT_MENU_WORLD_TYPE=flat|void|showcase and QTCRAFT_MENU_GAME_MODE=creative (or another mode):
// the new world's form `draft` chooses `worldType` and `gameMode` and starts with them whenever it
// opens (QcNewWorldDraft::setStartWorldType, setStartGameMode). Empty leaves a choice alone; an
// unknown name is refused with a warning.
void newWorld(QcNewWorldDraft &draft, const QString &worldType, const QString &gameMode);

} // namespace QcMenuPreselect

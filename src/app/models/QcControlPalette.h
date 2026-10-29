// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

class QQuickWindow;

// Puts the game's control palette (QcUiPalette::controlPalette, decision E-279) on a window. Popups,
// dialogs and the attached tooltip take their palette from their window, not from the item they
// belong to, and the window's palette overrides the system's palettes for tooltips and menus; so
// the main window and the offscreen window both get it.
namespace QcControlPalette {

// False when the window offers no palette (not a Qt Quick window of Qt 6.2 or later).
bool applyTo(QQuickWindow &window);

} // namespace QcControlPalette

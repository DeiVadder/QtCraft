// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtCraft

// The font of a window (`target`) at the screens' scale (decision E-725): controls, popups and
// tooltips inherit it. Off at 100 %, where the window keeps the style's own font; turning it off
// again (back to 100 % or a smaller window) gives that font back.
Binding {
    property: "font"
    value: UiScaling.font
    when: Ui.scale !== 1
    restoreMode: Binding.RestoreBindingOrValue
}

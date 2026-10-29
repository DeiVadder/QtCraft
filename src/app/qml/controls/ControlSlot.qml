// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick.Layouts

// The column of a SettingRow's controls (decision E-845): while the row dims, its controls do not
// dim themselves again (GameSwitch and GameButton ask their parent as a ControlSlot; a disabled
// GameButton outside a dimmed row turns grey instead).
RowLayout {
    property bool dimsControls
}

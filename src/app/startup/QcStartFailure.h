// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QStringList>

class QQmlApplicationEngine;

// When the main window cannot be loaded (decision E-964): a QML plugin of Qt was not found on the
// first start of a freshly unpacked Windows package (playtest 2026-11-02; the second start
// worked). A start by double-click then starts the program once more after a pause, and a start
// that fails again tells the player what to check (a message box on Windows) instead of ending
// without a word.
namespace QcStartFailure {

// Loads Main.qml into `engine`; on failure starts once more or reports (see above) and returns
// false.
[[nodiscard]] bool loadMainWindow(QQmlApplicationEngine &engine, bool offscreen);

// Starts the program once more after `pauseMs` (the files may still be written or scanned);
// returns whether it started.
[[nodiscard]] bool restart(int pauseMs);
// Logs `errors` with what to check and, for a plain start on Windows, shows it in a message box.
void report(const QStringList &errors, bool missingPart, bool offscreen);

} // namespace QcStartFailure

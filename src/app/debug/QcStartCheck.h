// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QProcessEnvironment>
#include <QQmlError>
#include <QString>
#include <QStringList>

// The decisions of a failed start of the main window (decision E-964, QcStartFailure in the
// program): whether the errors say that a part of Qt was missing, and whether this start may start
// the program once more. Pure functions, so tests can check them without starting the game.
namespace QcStartCheck {

// Set in the second start, so it never starts a third.
inline constexpr const char *retryVariable = "QTCRAFT_START_RETRY";

// The engine's warnings as lines for the log and the message.
[[nodiscard]] QStringList lines(const QList<QQmlError> &warnings);
// Whether `errors` say that a part of Qt was missing (a QML module's plugin not found, a library
// that does not load), rather than an error in the game's own QML.
[[nodiscard]] bool missingQtPart(const QStringList &errors);
// Whether the start was a plain one (double-clicked): not `offscreen`, no `arguments` after the
// program, no screenshot run in `environment`.
[[nodiscard]] bool plainStart(bool offscreen, const QStringList &arguments, const QProcessEnvironment &environment);
// Whether such a start may start the program once more: plain, and not itself the second start
// (retryVariable in `environment`).
[[nodiscard]] bool mayRestart(bool offscreen, const QStringList &arguments, const QProcessEnvironment &environment);

} // namespace QcStartCheck

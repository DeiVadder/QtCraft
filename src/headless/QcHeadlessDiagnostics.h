// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "headless/QcHeadlessOptions.h"

#include <QCommandLineParser>
#include <QStringList>

#include <memory>

class QcDiagnosticsSession;

// Logging, crash reports and diagnostics bundles of qtcraft-headless (decision E-834).
namespace QcHeadlessDiagnostics {

// The diagnostics of this run: a session log in <data>/logs with --server (or the file of
// --log-file), crash reports always. nullptr after printing why when an option is bad.
std::unique_ptr<QcDiagnosticsSession> start(const QCommandLineParser &parser, const QcHeadlessOptions::Options &options);
// Writes a diagnostics bundle of `dataDirectory` with the further `info` lines and prints its
// folder; false after printing why it failed.
bool writeBundle(const QString &dataDirectory, const QStringList &info);

} // namespace QcHeadlessDiagnostics

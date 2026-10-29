// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

// What build and machine a log, crash report or login comes from (decision E-834): the project
// version and the git commit at configure time (CMake writes both into QcBuildInfo.cpp), the
// compiler, Qt, the operating system and the processor.
namespace QcBuildInfo {

// The project version, e.g. "0.1.0".
QString version();
// The short git commit the build was configured at, with "-dirty" for local changes; "unknown"
// outside a git checkout.
QString gitCommit();
// "<program> <version> (git <commit>)".
QString describe(const QString &program);
// What a client tells the server at login (QcMsg::Hello::client): program, version, commit,
// operating system and processor architecture, e.g. "qtcraft 0.1.0 (git 1a2b3c4) Windows 11 x86_64".
QString clientText(const QString &program);
// The name of the processor (e.g. "Apple M2 Pro"); empty where the platform does not report it.
QString cpuName();
// The first lines of a log: build, compiler and Qt, operating system, processor, process id and start.
QStringList headerLines(const QString &program);

} // namespace QcBuildInfo

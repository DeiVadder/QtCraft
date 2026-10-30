// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

/*! \brief What build and machine a log, crash report or login comes from (decision E-834): the
    project version and the git commit at configure time (CMake writes both into
    QcBuildInfo.cpp), the compiler, Qt, the operating system and the processor. */
namespace QcBuildInfo {

/*! Returns the project version, e.g. "0.1.0". */
QString version();
/*! Returns the short git commit the build was configured at, with "-dirty" for local changes;
    "unknown" outside a git checkout. */
QString gitCommit();
/*! Returns "<program> <version> (git <commit>)" for \a program. */
QString describe(const QString &program);
/*! Returns what a client tells the server at login (QcMsg::Hello::client): \a program, version,
    commit, operating system and processor architecture, e.g. "qtcraft 0.1.0 (git 1a2b3c4)
    Windows 11 x86_64". */
QString clientText(const QString &program);
/*! Returns the name of the processor (e.g. "Apple M2 Pro"); empty where the platform does not
    report it. */
QString cpuName();
/*! Returns the first lines of a log of \a program: build, compiler and Qt, operating system,
    processor, process id and start. */
QStringList headerLines(const QString &program);

} // namespace QcBuildInfo

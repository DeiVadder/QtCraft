// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

/*! Console output of the programs (decision E-948): QString text goes to standard output or
    standard error as UTF-8 through \c std::print, without a \c std::string copy. Like
    \c std::print, nothing is flushed. */
namespace QcConsole {

/*! Writes \a text to standard output, without a line break. */
void print(const QString &text);

/*! Writes \a text and a line break to standard output. */
void printLine(const QString &text);

/*! Writes \a text and a line break to standard error. */
void printErrorLine(const QString &text);

} // namespace QcConsole

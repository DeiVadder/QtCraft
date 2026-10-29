// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// POSIX only: the default signal disposition without the C-style cast of SIG_DFL at the call site.

#include <csignal>

using QcSignalHandler = void (*)(int);

// SIG_DFL as a typed handler. glibc defines SIG_DFL as a C-style cast, which GCC reports with
// -Wold-style-cast at every use (Clang does not, the cast comes from a system header), so the
// macro is expanded only here, with the warning off for this function (E-865).
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif
inline QcSignalHandler qcDefaultSignalHandler()
{
    return SIG_DFL;
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

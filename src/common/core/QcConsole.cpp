// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/core/QcConsole.h"

#include <cstdio>
#include <print>
#include <string_view>

namespace {

// The UTF-8 bytes stay alive until the end of the full expression that prints them.
std::string_view view(const QByteArray &utf8)
{
    return {utf8.constData(), static_cast<std::size_t>(utf8.size())};
}

} // namespace

void QcConsole::print(const QString &text)
{
    std::print(stdout, "{}", view(text.toUtf8()));
}

void QcConsole::printLine(const QString &text)
{
    std::println(stdout, "{}", view(text.toUtf8()));
}

void QcConsole::printErrorLine(const QString &text)
{
    std::println(stderr, "{}", view(text.toUtf8()));
}

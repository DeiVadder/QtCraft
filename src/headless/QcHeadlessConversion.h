// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "headless/QcHeadlessOptions.h"

#include <QCommandLineParser>

#include <memory>

class QcContentRegistry;

/*! \brief \c {qtcraft-headless --convert-generator <world> [--to <version>]
    [--mode hard|blend] [--blend-chunks <n>] [--dimension <id>]} (decisions E-1014, E-1040):
    switches a saved world to a newer world generator before it is played, for dedicated servers
    and tests, the same conversion as the menu's
    (QcGeneratorUpgrade): a backup first, progress on standard output, the result and the backup's
    folder at the end. */
namespace QcHeadlessConversion {

/*! Converts the world the command line (\a options as \a parser read them) names with
    \a content. Returns the exit code: 0 when switched, 1 when refused or failed (the reason on
    standard error). */
[[nodiscard]] int run(const QCommandLineParser &parser, const QcHeadlessOptions::Options &options,
                      const std::shared_ptr<const QcContentRegistry> &content);

} // namespace QcHeadlessConversion

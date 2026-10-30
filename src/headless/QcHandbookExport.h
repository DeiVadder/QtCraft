// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "headless/QcHeadlessOptions.h"

#include <QCommandLineParser>

#include <memory>

class QcContentRegistry;

/*! \brief The handbook exports of qtcraft-headless: Markdown (--export-handbook, docs/handbook) and
    the data of the web handbook (--export-handbook-web, web/handbook/data).

    Both document the base game unless mods are asked for with --mods-dir; problems go to stderr.
    Return the exit code. */
namespace QcHandbookExport {

/*! Writes the handbook in the language of --language as Markdown files into the directory of
    --export-handbook (both from \a options as \a parser read them); documents \a content with
    --mods-dir, else the base game. Returns 0 on success, else 1. */
int markdown(const QCommandLineParser &parser, const QcHeadlessOptions::Options &options,
             const std::shared_ptr<const QcContentRegistry> &content);
/*! Writes every language of the content, the icon sheet and theme.css (QcHandbookWeb,
    QcWebHandbookAssets) into the directory of --export-handbook-web (from \a options as
    \a parser read them); documents \a content with --mods-dir, else the base game. Returns 0
    on success, 1 when writing fails or an icon is missing. */
int web(const QCommandLineParser &parser, const QcHeadlessOptions::Options &options, const std::shared_ptr<const QcContentRegistry> &content);

} // namespace QcHandbookExport

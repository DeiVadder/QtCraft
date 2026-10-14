#pragma once

#include "QcHeadlessOptions.h"

#include <QCommandLineParser>

#include <memory>

class QcContentRegistry;

// The handbook exports of qtcraft-headless: Markdown (--export-handbook, docs/handbook) and the data
// of the web handbook (--export-handbook-web, web/handbook/data). Both document the base game unless
// mods are asked for with --mods-dir; problems go to stderr. Return the exit code.
namespace QcHandbookExport {

int markdown(const QCommandLineParser &parser, const QcHeadlessOptions::Options &options,
             const std::shared_ptr<const QcContentRegistry> &content);
// Every language of the content, the icon sheet and theme.css (QcHandbookWeb, QcWebHandbookAssets).
int web(const QCommandLineParser &parser, const QcHeadlessOptions::Options &options, const std::shared_ptr<const QcContentRegistry> &content);

} // namespace QcHandbookExport

// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "headless/QcHeadlessConversion.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcConsole.h"
#include "server/dimensions/QcDimensions.h"
#include "server/storage/QcWorldStorage.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worlds/QcGeneratorUpgrade.h"
#include "worlds/QcWorldLibrary.h"

#include <QDir>
#include <QFileInfo>

#include <array>

using namespace Qt::StringLiterals;

namespace {

using QcHeadlessOptions::Options;
using QcHeadlessOptions::printError;

// The world folder `name` names: a path to a world, else a folder of the data directory's worlds.
QcResult<QString> worldDirectoryOf(const QString &name, const QCommandLineParser &parser, const Options &options)
{
    if (QFileInfo(QDir(name).filePath(QString::fromLatin1(QcWorldStorage::infoFileName))).isFile()) {
        return QDir(name).absolutePath();
    }
    const QcWorldLibrary library(QDir(QcHeadlessOptions::dataDirectory(parser, options)).filePath(u"worlds"_s));
    return library.worldDirectory(name);
}

// The switch the command line asks for in the world `directory` with `content`.
QcResult<QcGeneratorUpgrade::Change> changeOf(const QString &directory, const QCommandLineParser &parser, const Options &options,
                                               const std::shared_ptr<const QcContentRegistry> &content)
{
    const QcResult<QcWorldInfo> info = QcWorldStorage(directory, content).readInfo();
    if (!info) {
        return qcError(info.error());
    }
    const QString name = parser.value(options.convertDimension);
    const bool overworld = name == QLatin1StringView(QcDimensionDefinition::overworldName);
    const QcDimensionDefinition *dimension = overworld ? &content->worldgen().overworld() : content->worldgen().dimension(name);
    if (dimension == nullptr) {
        return qcError(u"There is no dimension '%1'"_s.arg(name));
    }
    const QcGeneratorVersion::Series series = QcWorldGenerators::versionSeries(QcDimensions::generatorTypeIn(*info, *dimension));
    bool ok = true;
    const int to = parser.isSet(options.convertTo) ? parser.value(options.convertTo).toInt(&ok) : series.current;
    if (!ok) {
        return qcError(u"--to needs a version number"_s);
    }
    return QcGeneratorUpgrade::Change{name, QcDimensions::versionIn(*info, *dimension), to};
}

// The progress line of `step`.
void printStep(const QcConversionControl::Step &step)
{
    static constexpr std::array names{"backup", "survey", "blend", "write"};
    QcConsole::printLine(u"%1 %2/%3"_s.arg(QLatin1StringView(names.at(static_cast<std::size_t>(step.stage)))).arg(step.done).arg(step.total));
}

} // namespace

namespace QcHeadlessConversion {

int run(const QCommandLineParser &parser, const Options &options, const std::shared_ptr<const QcContentRegistry> &content)
{
    const std::optional<QcGeneratorUpgrade::Mode> mode = QcGeneratorUpgrade::modeNamed(parser.value(options.convertMode));
    if (!mode) {
        printError(u"--mode takes hard or blend, not '%1'"_s.arg(parser.value(options.convertMode)));
        return 1;
    }
    const QcResult<QString> directory = worldDirectoryOf(parser.value(options.convertGenerator), parser, options);
    if (!directory) {
        printError(directory.error());
        return 1;
    }
    const QcResult<QcGeneratorUpgrade::Change> change = changeOf(*directory, parser, options, content);
    if (!change) {
        printError(change.error());
        return 1;
    }

    bool widthRead = true;
    const int blendChunks = parser.value(options.convertBlendChunks).toInt(&widthRead);
    if (!widthRead) {
        printError(u"--blend-chunks needs a number"_s);
        return 1;
    }

    const QcConversionControl control{printStep, {}};
    const QcResult<QcGeneratorUpgrade::Converted> converted = QcGeneratorUpgrade::convert({*directory, {*change}, *mode, blendChunks, {}}, content, control);
    if (!converted) {
        printError(converted.error());
        return 1;
    }
    for (const QString &warning : converted->warnings) {
        printError(u"warning: "_s + warning);
    }
    QcConsole::printLine(u"switched %1 from version %2 to %3, %4 chunks keep version %2, %5 chunks blend; backup %6"_s.arg(change->dimension)
                             .arg(change->from)
                             .arg(change->to)
                             .arg(converted->keptChunks)
                             .arg(converted->ringChunks)
                             .arg(converted->backup));
    return 0;
}

} // namespace QcHeadlessConversion

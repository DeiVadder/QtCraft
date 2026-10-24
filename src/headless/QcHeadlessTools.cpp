#include "headless/QcHeadlessTools.h"

#include "clientcore/icons/QcAppIcon.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/loader/QcBaseContent.h"
#include "headless/QcHandbookExport.h"
#include "headless/QcMapRenderer.h"
#include "headless/QcShowcaseTour.h"
#include "headless/QcSoundExporter.h"
#include "headless/QcTextureTools.h"
#include "headless/QcWorldStats.h"
#include "worldgen/showcase/QcShowcaseGenerator.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QCoreApplication>
#include <QTimer>

#include <print>

using namespace Qt::StringLiterals;

namespace {

using QcHeadlessOptions::Options;
using QcHeadlessOptions::printError;
using QcHeadlessOptions::printLines;
using QcHeadlessTools::exitSoon;

int runTextureTool(const QCommandLineParser &parser, const Options &options, const QcContentRegistry &content)
{
    const QString mods = parser.isSet(options.noMods) ? QString() : parser.value(options.mods);
    if (parser.isSet(options.atlasStats)) {
        return QcTextureTools::printAtlasStats(content, mods);
    }

    const auto list = [&parser](const QCommandLineOption &option) { return parser.value(option).split(u',', Qt::SkipEmptyParts); };
    return QcTextureTools::exportPack(content, {parser.value(options.exportTextures), parser.value(options.textureResolution).toInt(),
                                                list(options.texturePacks), list(options.textureKeys), parser.value(options.textureName), mods});
}

void exportSounds(const QcContentRegistry &content, const QString &directory, quint64 seed)
{
    const QcResult<QStringList> exported = QcSoundExporter::exportAll(content, directory, seed);
    if (exported) {
        printLines(*exported);
    } else {
        printError(exported.error());
    }
    exitSoon(exported ? 0 : 1);
}

void saveMap(const QImage &map, const QString &file)
{
    const bool saved = map.save(file);
    if (!saved) {
        printError(u"Cannot write map"_s);
    }
    exitSoon(saved ? 0 : 1);
}

// Whether a dry column and the columns 32 blocks around it lie in `biome`.
bool wellInside(const QcTerrainGenerator &generator, int biome, int x, int z)
{
    const auto inside = [&](int cx, int cz) {
        const QcTerrainGenerator::Column column = generator.column(cx, cz);
        return column.biome == biome && column.height > generator.seaLevel() + 1;
    };
    return inside(x, z) && inside(x + 32, z) && inside(x - 32, z) && inside(x, z + 32) && inside(x, z - 32);
}

// The first column well inside `biome` on the square ring of `ring` blocks around the origin.
[[nodiscard]] std::optional<QString> legendPlaceOnRing(const QcTerrainGenerator &generator, int biome, int ring)
{
    for (int dz = -ring; dz <= ring; dz += 16) {
        for (int dx = -ring; dx <= ring; dx += 16) {
            if (std::max(std::abs(dx), std::abs(dz)) == ring && wellInside(generator, biome, dx, dz)) {
                return u"at %1,%2"_s.arg(dx).arg(dz);
            }
        }
    }
    return std::nullopt;
}

// The nearest dry column of a biome that lies well inside it, for the biome map's legend.
QString biomeLegendPlace(const QcTerrainGenerator &generator, int biome, int radius)
{
    for (int ring = 0; ring <= radius; ring += 16) {
        if (const std::optional<QString> place = legendPlaceOnRing(generator, biome, ring)) {
            return *place;
        }
    }
    return u"not found"_s;
}

void writeBiomeMap(const QcTerrainGenerator &generator, const QcContentRegistry &content, QcMapRenderer::Area area, const QString &file)
{
    const int radius = area.radius;
    const bool saved = QcMapRenderer::renderBiomes(generator, area).save(file);
    if (!saved) {
        printError(u"Cannot write map"_s);
    }
    for (qsizetype i = 0; i < content.worldgen().biomes().size(); ++i) {
        std::println("{} {} {}", i, generator.biomeName(static_cast<int>(i)).toStdString(),
            biomeLegendPlace(generator, static_cast<int>(i), radius).toStdString());
    }
    exitSoon(saved ? 0 : 1);
}

// The centre of the maps (--map-centre "x,z"); the origin when the value does not parse.
std::pair<int, int> mapCentre(const QCommandLineParser &parser, const Options &options)
{
    const QStringList parts = parser.value(options.mapCentre).split(u',');
    bool okX = false;
    bool okZ = false;
    const int x = parts.size() == 2 ? parts[0].trimmed().toInt(&okX) : 0;
    const int z = parts.size() == 2 ? parts[1].trimmed().toInt(&okZ) : 0;
    if (!okX || !okZ) {
        printError(u"--map-centre needs x,z; using the origin"_s);
        return {0, 0};
    }
    return {x, z};
}

// The world generation tools (--world-stats, --slice-map, --biome-map, --map); false when none
// was asked for.
bool runWorldTool(const QCommandLineParser &parser, const Options &options, const QcTerrainGenerator &generator,
                  const QcContentRegistry &content)
{
    if (parser.isSet(options.stats)) {
        const int chunks = std::clamp(parser.value(options.statsRadius).toInt(), 1, 64);
        const int biomeRadius = std::clamp(parser.value(options.mapRadius).toInt(), 256, 16384);
        printLines(QcWorldStats::report(generator, content, chunks, biomeRadius));
        exitSoon(0);
        return true;
    }

    const auto [centreX, centreZ] = mapCentre(parser, options);
    if (parser.isSet(options.slice)) {
        const int radius = std::clamp(parser.value(options.mapRadius).toInt(), 16, 1024);
        saveMap(QcMapRenderer::renderSlice(generator, content, {centreX, centreZ, radius, 4}), parser.value(options.slice));
        return true;
    }

    if (parser.isSet(options.biomeMap)) {
        const int radius = std::clamp(parser.value(options.mapRadius).toInt(), 64, 8192);
        writeBiomeMap(generator, content, {centreX, centreZ, radius, std::max(1, radius / 512)}, parser.value(options.biomeMap));
        return true;
    }

    if (parser.isSet(options.map)) {
        const int radius = std::clamp(parser.value(options.mapRadius).toInt(), 64, 8192);
        saveMap(QcMapRenderer::render(generator, content, {centreX, centreZ, radius, std::max(1, radius / 512)}), parser.value(options.map));
        return true;
    }

    return false;
}

// --tour: the stops of the showcase tour.
void printTour(const std::shared_ptr<const QcContentRegistry> &content)
{
    const QcShowcaseGenerator showcase(content);
    for (const QString &warning : showcase.warnings()) {
        printError(u"world generation warning: "_s + warning);
    }
    printLines(QcShowcaseTour::lines(showcase.plan()));
    exitSoon(0);
}

// The tools that need the content but no world generator; false when none was asked for.
bool runContentTool(const QCommandLineParser &parser, const Options &options, quint64 seed,
                    const std::shared_ptr<const QcContentRegistry> &content)
{
    if (parser.isSet(options.sounds)) {
        exportSounds(*content, parser.value(options.sounds), seed);
        return true;
    }
    if (parser.isSet(options.handbook)) {
        exitSoon(QcHandbookExport::markdown(parser, options, content));
        return true;
    }
    if (parser.isSet(options.handbookWeb)) {
        exitSoon(QcHandbookExport::web(parser, options, content));
        return true;
    }
    if (parser.isSet(options.exportTextures) || parser.isSet(options.atlasStats)) {
        exitSoon(runTextureTool(parser, options, *content));
        return true;
    }
    if (parser.isSet(options.tour)) {
        printTour(content);
        return true;
    }
    return false;
}

} // namespace

namespace QcHeadlessTools {

void exitSoon(int code)
{
    QTimer::singleShot(0, qApp, [code] { QCoreApplication::exit(code); });
}

bool exportIcon(const QString &path)
{
    const QcResult<QcBaseContent::Loaded> base = QcBaseContent::load();
    if (!base) {
        printError(u"Cannot load the base game: "_s + base.error());
        return false;
    }

    // A path ending in .ico gets the Windows icon (cmake/windows/QtCraft.ico), any other an .iconset.
    const QcTextureAtlas atlas = QcTextureAtlas::build(*base->registry);
    const bool ico = path.endsWith(u".ico"_s, Qt::CaseInsensitive);
    const QcStatus written = ico ? QcAppIcon::writeIco(atlas, path) : QcAppIcon::writeIconSet(atlas, path);
    if (!written) {
        printError(written.error());
    }
    exitSoon(written ? 0 : 1);
    return true;
}

bool run(const QCommandLineParser &parser, const Options &options, const QcServerConfig &config,
         const std::shared_ptr<const QcContentRegistry> &content)
{
    if (runContentTool(parser, options, static_cast<quint64>(config.seed), content)) {
        return true;
    }

    const QcTerrainGenerator generator(content, config.seed);
    for (const QString &warning : generator.warnings()) {
        printError(u"world generation warning: "_s + warning);
    }
    return runWorldTool(parser, options, generator, *content);
}

} // namespace QcHeadlessTools

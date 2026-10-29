#include "worldgen/generators/QcWorldGenerators.h"

#include "worldgen/generators/QcFlatGenerator.h"
#include "worldgen/generators/QcVoidGenerator.h"
#include "worldgen/showcase/QcShowcaseGenerator.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

using namespace Qt::StringLiterals;

namespace {

// A generator of `type`; nullptr for unknown types.
std::unique_ptr<QcWorldGenerator> makeGenerator(const QString &type, std::shared_ptr<const QcContentRegistry> content, qint64 seed,
                                                const QString &options)
{
    using namespace QcWorldGenerators;
    if (type == QLatin1StringView(terrain)) {
        return std::make_unique<QcTerrainGenerator>(std::move(content), seed);
    }
    if (type == QLatin1StringView(flat)) {
        return std::make_unique<QcFlatGenerator>(std::move(content), options);
    }
    if (type == QLatin1StringView(empty)) {
        return std::make_unique<QcVoidGenerator>(std::move(content), options);
    }
    if (type == QLatin1StringView(showcase)) {
        return std::make_unique<QcShowcaseGenerator>(std::move(content));
    }
    return nullptr;
}

} // namespace

namespace QcWorldGenerators {

QStringList types()
{
    return {QString::fromLatin1(terrain), QString::fromLatin1(flat), QString::fromLatin1(empty), QString::fromLatin1(showcase)};
}

bool isKnown(const QString &type)
{
    return types().contains(type);
}

[[nodiscard]] QcStatus checkOptions(const QString &type, const QString &options, const QcContentRegistry &content)
{
    if (!isKnown(type)) {
        return qcError(u"Unknown world type '%1' (%2)"_s.arg(type, types().join(u", "_s)));
    }
    if (options.size() > maxOptionsLength) {
        return qcError(u"The options are longer than %1 characters"_s.arg(maxOptionsLength));
    }
    if (options.isEmpty()) {
        return {};
    }

    if (type == QLatin1StringView(flat)) {
        if (const QcResult<QList<QcBlockId>> layers = QcFlatGenerator::parseLayers(options, content); !layers) {
            return qcError(layers.error());
        }
        return {};
    }
    if (type == QLatin1StringView(empty)) {
        if (const QcResult<QcVoidDefinition> platform = QcVoidGenerator::parseOptions(options, content); !platform) {
            return qcError(platform.error());
        }
        return {};
    }
    return qcError(u"The world type '%1' takes no options"_s.arg(type));
}

[[nodiscard]] QcResult<QString> optionsToStore(const QString &type, const QString &options, const QcContentRegistry &content)
{
    if (const QcStatus checked = checkOptions(type, options, content); !checked) {
        return qcError(checked.error());
    }

    if (type == QLatin1StringView(flat)) {
        return options.isEmpty() ? content.worldgen().flatWorld().preset : options;
    }
    if (type == QLatin1StringView(empty)) {
        return QcVoidGenerator::optionsOf(*QcVoidGenerator::parseOptions(options, content));
    }
    return options;
}

std::unique_ptr<QcWorldGenerator> create(const QString &type, std::shared_ptr<const QcContentRegistry> content, qint64 seed,
                                         const QString &options, int version)
{
    if (!QcGeneratorVersion::isKnown(version)) {
        return nullptr;
    }
    std::unique_ptr<QcWorldGenerator> generator = makeGenerator(type, std::move(content), seed, options);
    // Every version so far generates with the same code (decision E-922): a version that changes
    // the output branches on version() where it does. The showcase is always the current one.
    if (generator) {
        generator->setVersion(type == QLatin1StringView(showcase) ? QcGeneratorVersion::current : version);
    }
    return generator;
}

} // namespace QcWorldGenerators

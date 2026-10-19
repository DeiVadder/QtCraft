#include "worldgen/generators/QcVoidGenerator.h"

#include "common/content/registry/QcContentNames.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// The limits of worldgen/void.json.
constexpr int maxRadius = 16;
constexpr int minY = 1;
constexpr int maxY = QcWorld::worldHeight - 4;

// `value` as a number in [minimum, maximum].
[[nodiscard]] std::optional<int> numberIn(const QString &value, int minimum, int maximum)
{
    bool ok = false;
    const int number = value.toInt(&ok);
    return ok && number >= minimum && number <= maximum ? std::optional(number) : std::nullopt;
}

} // namespace

QcResult<QcVoidDefinition> QcVoidGenerator::parseOptions(const QString &options, const QcContentRegistry &content)
{
    QcVoidDefinition settings = content.voidWorld();
    const QStringList entries = options.split(u',', Qt::SkipEmptyParts);
    for (const QString &entry : entries) {
        const qsizetype equals = entry.indexOf(u'=');
        const QString key = entry.left(equals).trimmed();
        const QString value = entry.mid(equals + 1).trimmed();
        bool valid = equals >= 0;
        if (key == u"platform") {
            settings.platform = QcContentNames::qualify(value, u"qtcraft"_s);
            valid = valid && !value.isEmpty() && content.blockId(settings.platform).has_value();
        } else if (key == u"radius") {
            const std::optional<int> radius = numberIn(value, 0, maxRadius);
            settings.platformRadius = radius.value_or(0);
            valid = valid && radius.has_value();
        } else if (key == u"y") {
            const std::optional<int> y = numberIn(value, minY, maxY);
            settings.platformY = y.value_or(minY);
            valid = valid && y.has_value();
        } else {
            valid = false;
        }
        if (!valid) {
            return qcError(u"Invalid platform option '%1' (expected platform=<block>, radius=0..%2 or y=%3..%4)"_s.arg(entry.trimmed())
                               .arg(maxRadius)
                               .arg(minY)
                               .arg(maxY));
        }
    }
    return settings;
}

QString QcVoidGenerator::optionsOf(const QcVoidDefinition &settings)
{
    return u"platform=%1,radius=%2,y=%3"_s.arg(settings.platform).arg(settings.platformRadius).arg(settings.platformY);
}

QcVoidGenerator::QcVoidGenerator(std::shared_ptr<const QcContentRegistry> content, const QString &options)
    : m_content(std::move(content))
{
    QcResult<QcVoidDefinition> parsed = parseOptions(options, *m_content);
    if (!parsed) {
        m_warnings.append(u"void world: %1; using the default platform"_s.arg(parsed.error()));
        parsed = m_content->voidWorld();
    }
    const QcVoidDefinition &settings = *parsed;
    m_platform = m_content->blockId(settings.platform).value_or(m_content->blockId(QStringLiteral("qtcraft:stone")).value_or(qcAirBlockId));
    m_radius = settings.platformRadius;
    m_platformY = settings.platformY;
}

std::shared_ptr<QcChunk> QcVoidGenerator::generate(QcChunkPos pos) const
{
    auto chunk = std::make_shared<QcChunk>(pos);
    if (m_radius <= 0 || m_platform == qcAirBlockId) {
        return chunk;
    }

    // The platform covers -radius..radius around the origin; most chunks stay empty.
    const int baseX = pos.x * QcWorld::chunkSize;
    const int baseZ = pos.z * QcWorld::chunkSize;
    for (int x = std::max(-m_radius, baseX); x <= std::min(m_radius, baseX + QcWorld::chunkSize - 1); ++x) {
        for (int z = std::max(-m_radius, baseZ); z <= std::min(m_radius, baseZ + QcWorld::chunkSize - 1); ++z) {
            chunk->setBlock(x - baseX, m_platformY, z - baseZ, m_platform);
        }
    }
    return chunk;
}

QcWorldSetup QcVoidGenerator::setup() const
{
    return QcWorldSetup::fromRules(m_content->voidWorld().rules);
}

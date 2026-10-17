#include "worldgen/generators/QcFlatGenerator.h"

#include "common/content/registry/QcContentNames.h"

using namespace Qt::StringLiterals;

namespace {

// Room kept above the layers.
constexpr int headroom = 16;

} // namespace

QcResult<QList<QcBlockId>> QcFlatGenerator::parseLayers(const QString &preset, const QcContentRegistry &content)
{
    QList<QcBlockId> layers;
    const QStringList entries = preset.split(u',', Qt::SkipEmptyParts);
    for (const QString &raw : entries) {
        const QString entry = raw.trimmed();
        const qsizetype star = entry.indexOf(u'*');
        bool countOk = true;
        const int count = star < 0 ? 1 : entry.left(star).trimmed().toInt(&countOk);
        const QString name = QcContentNames::qualify(entry.mid(star + 1).trimmed(), u"qtcraft"_s);
        const std::optional<QcBlockId> block = name.isEmpty() ? std::nullopt : content.blockId(name);
        if (!countOk || count < 1 || !block) {
            return qcError(u"Invalid layer '%1' (expected [count*]block)"_s.arg(entry));
        }
        if (layers.size() + count > QcWorld::worldHeight - headroom) {
            return qcError(u"The layers are higher than %1 blocks"_s.arg(QcWorld::worldHeight - headroom));
        }
        layers.insert(layers.size(), count, *block);
    }

    if (layers.isEmpty()) {
        return qcError(u"No layers"_s);
    }
    return layers;
}

QcFlatGenerator::QcFlatGenerator(std::shared_ptr<const QcContentRegistry> content, const QString &preset, QStringList *warnings)
    : m_content(std::move(content))
{
    QcResult<QList<QcBlockId>> layers = parseLayers(preset.isEmpty() ? m_content->flatWorld().preset : preset, *m_content);
    if (!layers && !preset.isEmpty()) {
        if (warnings) {
            warnings->append(u"flat world: %1; using the default layers"_s.arg(layers.error()));
        }
        layers = parseLayers(m_content->flatWorld().preset, *m_content);
    }
    if (!layers) {
        if (warnings) {
            warnings->append(u"flat world: %1; using stone"_s.arg(layers.error()));
        }
        layers = QList<QcBlockId>{m_content->blockId(u"qtcraft:stone"_s).value_or(qcAirBlockId)};
    }
    m_layers = std::move(*layers);
}

std::shared_ptr<QcChunk> QcFlatGenerator::generate(QcChunkPos pos) const
{
    auto chunk = std::make_shared<QcChunk>(pos);
    for (int y = 0; y < m_layers.size(); ++y) {
        for (int z = 0; z < QcWorld::chunkSize; ++z) {
            for (int x = 0; x < QcWorld::chunkSize; ++x) {
                chunk->setBlock(x, y, z, m_layers[y]);
            }
        }
    }
    return chunk;
}

QcWorldSetup QcFlatGenerator::setup() const
{
    return QcWorldSetup::fromRules(m_content->flatWorld().rules);
}

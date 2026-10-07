#include "QcContentRegistry.h"

#include "QcContentNames.h"
#include "QcStreamLimits.h"

#include <QJsonDocument>

using namespace Qt::StringLiterals;

namespace {

void writeBlock(QDataStream &stream, const QcBlockDefinition &block)
{
    stream << block.name << block.translationKey << quint8(block.renderLayer) << block.solid << block.opaque
           << block.liquid << block.replaceable << qint32(block.lightOpacity) << block.hardness << block.tool
           << qint32(block.toolLevel);
    stream << quint32(block.drops.size());
    for (const QcItemDrop &drop : block.drops) {
        stream << drop.item << qint32(drop.count);
    }
    for (const QString &texture : block.faceTextures) {
        stream << texture;
    }
}

bool readBlock(QDataStream &stream, QcBlockDefinition &block)
{
    quint8 renderLayer = 0;
    qint32 lightOpacity = 0;
    qint32 toolLevel = 0;
    stream >> block.name >> block.translationKey >> renderLayer >> block.solid >> block.opaque >> block.liquid
        >> block.replaceable >> lightOpacity >> block.hardness >> block.tool >> toolLevel;
    if (renderLayer > quint8(QcRenderLayer::Translucent) || lightOpacity < 0 || lightOpacity > 15) {
        stream.setStatus(QDataStream::ReadCorruptData);
        return false;
    }
    block.renderLayer = QcRenderLayer(renderLayer);
    block.lightOpacity = lightOpacity;
    block.toolLevel = toolLevel;
    quint32 dropCount = 0;
    if (!QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions, dropCount)) {
        return false;
    }
    block.drops.clear();
    for (quint32 i = 0; i < dropCount; ++i) {
        QcItemDrop drop;
        qint32 count = 0;
        stream >> drop.item >> count;
        drop.count = count;
        block.drops.append(drop);
    }
    for (QString &texture : block.faceTextures) {
        stream >> texture;
    }
    return stream.status() == QDataStream::Ok;
}

} // namespace

QcContentRegistry::QcContentRegistry()
{
    // Air is the only block the core defines itself: chunk storage relies on it as the empty value.
    QcBlockDefinition air;
    air.name = QString::fromLatin1(airName);
    air.translationKey = u"block.qtcraft.air"_s;
    air.renderLayer = QcRenderLayer::Invisible;
    air.solid = false;
    air.opaque = false;
    air.replaceable = true;
    air.lightOpacity = 0;
    air.hardness = -1.0;
    m_blocks.append(air);
    m_blockIds.insert(air.name, qcAirBlockId);
}

QcResult<QcBlockId> QcContentRegistry::registerBlock(QcBlockDefinition definition)
{
    if (!QcContentNames::isValidQualified(definition.name)) {
        return qcError(u"Invalid block name '%1'"_s.arg(definition.name));
    }
    if (definition.name == QLatin1StringView(airName)) {
        return qcError(u"The air block cannot be redefined"_s);
    }
    if (definition.lightOpacity < 0 || definition.lightOpacity > 15) {
        return qcError(u"Block '%1': lightOpacity must be within 0..15"_s.arg(definition.name));
    }
    const auto existing = m_blockIds.constFind(definition.name);
    if (existing != m_blockIds.constEnd()) {
        m_blocks[*existing] = std::move(definition);
        return *existing;
    }
    const auto id = QcBlockId(m_blocks.size());
    m_blockIds.insert(definition.name, id);
    m_blocks.append(std::move(definition));
    return id;
}

void QcContentRegistry::registerTexture(QcTextureDefinition definition)
{
    const auto existing = m_textureIndex.constFind(definition.name);
    if (existing != m_textureIndex.constEnd()) {
        m_textures[*existing] = std::move(definition);
        return;
    }
    m_textureIndex.insert(definition.name, m_textures.size());
    m_textures.append(std::move(definition));
}

void QcContentRegistry::addTranslations(const QString &language, const QHash<QString, QString> &entries)
{
    QHash<QString, QString> &table = m_translations[language];
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
        table.insert(it.key(), it.value());
    }
}

const QcBlockDefinition &QcContentRegistry::block(QcBlockId id) const
{
    Q_ASSERT(isValidBlock(id));
    return m_blocks[qsizetype(id)];
}

std::optional<QcBlockId> QcContentRegistry::blockId(const QString &name) const
{
    const auto it = m_blockIds.constFind(name);
    if (it == m_blockIds.constEnd()) {
        return std::nullopt;
    }
    return *it;
}

const QcTextureDefinition *QcContentRegistry::texture(const QString &name) const
{
    const auto it = m_textureIndex.constFind(name);
    return it == m_textureIndex.constEnd() ? nullptr : &m_textures[*it];
}

QString QcContentRegistry::translate(const QString &key, const QString &language) const
{
    for (const QString &candidate : {language, QString::fromLatin1(fallbackLanguage)}) {
        const auto table = m_translations.constFind(candidate);
        if (table != m_translations.constEnd()) {
            const auto entry = table->constFind(key);
            if (entry != table->constEnd()) {
                return *entry;
            }
        }
    }
    return key;
}

void QcContentRegistry::writeTo(QDataStream &stream) const
{
    stream << quint32(m_blocks.size());
    for (const QcBlockDefinition &block : m_blocks) {
        writeBlock(stream, block);
    }
    stream << quint32(m_textures.size());
    for (const QcTextureDefinition &texture : m_textures) {
        stream << texture.name << QJsonDocument(texture.procedural).toJson(QJsonDocument::Compact) << texture.png;
    }
    stream << quint32(m_translations.size());
    for (auto language = m_translations.constBegin(); language != m_translations.constEnd(); ++language) {
        stream << language.key() << quint32(language->size());
        for (auto entry = language->constBegin(); entry != language->constEnd(); ++entry) {
            stream << entry.key() << entry.value();
        }
    }
}

QcResult<QcContentRegistry> QcContentRegistry::readFrom(QDataStream &stream)
{
    QcContentRegistry registry;
    registry.m_blocks.clear();
    registry.m_blockIds.clear();

    quint32 blockCount = 0;
    if (!QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions, blockCount) || blockCount == 0) {
        return qcError(u"Corrupt content: block table"_s);
    }
    for (quint32 i = 0; i < blockCount; ++i) {
        QcBlockDefinition block;
        if (!readBlock(stream, block) || registry.m_blockIds.contains(block.name)) {
            return qcError(u"Corrupt content: block %1"_s.arg(i));
        }
        registry.m_blockIds.insert(block.name, QcBlockId(i));
        registry.m_blocks.append(std::move(block));
    }
    if (registry.m_blocks.constFirst().name != QLatin1StringView(airName)) {
        return qcError(u"Corrupt content: block 0 must be air"_s);
    }

    quint32 textureCount = 0;
    if (!QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions, textureCount)) {
        return qcError(u"Corrupt content: texture table"_s);
    }
    for (quint32 i = 0; i < textureCount; ++i) {
        QcTextureDefinition texture;
        QByteArray procedural;
        stream >> texture.name >> procedural >> texture.png;
        if (stream.status() != QDataStream::Ok) {
            return qcError(u"Corrupt content: texture %1"_s.arg(i));
        }
        texture.procedural = QJsonDocument::fromJson(procedural).object();
        registry.registerTexture(std::move(texture));
    }

    quint32 languageCount = 0;
    if (!QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions, languageCount)) {
        return qcError(u"Corrupt content: translations"_s);
    }
    for (quint32 i = 0; i < languageCount; ++i) {
        QString language;
        quint32 entryCount = 0;
        stream >> language;
        if (!QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions, entryCount)) {
            return qcError(u"Corrupt content: translations"_s);
        }
        QHash<QString, QString> entries;
        for (quint32 entry = 0; entry < entryCount; ++entry) {
            QString key;
            QString text;
            stream >> key >> text;
            entries.insert(key, text);
        }
        registry.addTranslations(language, entries);
    }
    if (stream.status() != QDataStream::Ok) {
        return qcError(u"Corrupt content: truncated"_s);
    }
    return registry;
}

#include "QcContentRegistry.h"

#include <cmath>

#include "QcContentNames.h"
#include "QcStreamLimits.h"

#include <QJsonDocument>

#include <algorithm>

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

QcResult<QcItemId> QcContentRegistry::registerItem(QcItemDefinition definition)
{
    if (!QcContentNames::isValidQualified(definition.name)) {
        return qcError(u"Invalid item name '%1'"_s.arg(definition.name));
    }
    if (definition.maxStack < 1 || definition.maxStack > 999) {
        return qcError(u"Item '%1': maxStack must be within 1..999"_s.arg(definition.name));
    }
    const auto existing = m_itemIds.constFind(definition.name);
    if (existing != m_itemIds.constEnd()) {
        m_items[qsizetype(*existing) - 1] = std::move(definition);
        return *existing;
    }
    const auto id = QcItemId(m_items.size() + 1);
    m_itemIds.insert(definition.name, id);
    m_items.append(std::move(definition));
    return id;
}

void QcContentRegistry::createBlockItems()
{
    for (const QcBlockDefinition &block : std::as_const(m_blocks)) {
        if (block.renderLayer == QcRenderLayer::Invisible || block.liquid || m_itemIds.contains(block.name)) {
            continue;
        }
        QcItemDefinition item;
        item.name = block.name;
        item.translationKey = block.translationKey;
        item.texture = block.texture(QcBlockFace::Up);
        item.placesBlock = block.name;
        // Block names are already validated, so registering the item cannot fail.
        [[maybe_unused]] const QcResult<QcItemId> id = registerItem(std::move(item));
        Q_ASSERT(id.has_value());
    }
}

const QcItemDefinition &QcContentRegistry::item(QcItemId id) const
{
    Q_ASSERT(isValidItem(id));
    return m_items[qsizetype(id) - 1];
}

std::optional<QcItemId> QcContentRegistry::itemId(const QString &name) const
{
    const auto it = m_itemIds.constFind(name);
    if (it == m_itemIds.constEnd()) {
        return std::nullopt;
    }
    return *it;
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

namespace {

template<typename Definition>
void registerByName(QList<Definition> &list, Definition definition)
{
    const auto existing = std::ranges::find(list, definition.name, &Definition::name);
    if (existing != list.end()) {
        *existing = std::move(definition);
    } else {
        list.append(std::move(definition));
    }
}

} // namespace

void QcContentRegistry::registerMob(QcMobDefinition definition)
{
    registerByName(m_mobs, std::move(definition));
}

std::optional<int> QcContentRegistry::mobIndex(const QString &name) const
{
    const auto it = std::ranges::find(m_mobs, name, &QcMobDefinition::name);
    if (it == m_mobs.end()) {
        return std::nullopt;
    }
    return int(it - m_mobs.begin());
}

void QcContentRegistry::registerBiome(QcBiomeDefinition definition)
{
    registerByName(m_biomes, std::move(definition));
}

void QcContentRegistry::registerFeature(QcFeatureDefinition definition)
{
    registerByName(m_features, std::move(definition));
}

void QcContentRegistry::registerOre(QcOreDefinition definition)
{
    registerByName(m_ores, std::move(definition));
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
    stream << quint32(m_items.size());
    for (const QcItemDefinition &item : m_items) {
        stream << item.name << item.translationKey << item.texture << qint32(item.maxStack) << item.placesBlock << item.tool
               << qint32(item.toolLevel) << item.toolSpeed << qint32(item.durability) << item.attackDamage;
    }
    stream << quint32(m_mobs.size());
    for (const QcMobDefinition &mob : m_mobs) {
        stream << mob.name << mob.translationKey << mob.maxHealth << mob.width << mob.height << mob.hostile
               << quint32(mob.parts.size());
        for (const QcMobPart &part : mob.parts) {
            stream << part.from.x << part.from.y << part.from.z << part.to.x << part.to.y << part.to.z << part.texture;
        }
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

    quint32 itemCount = 0;
    if (!QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions, itemCount)) {
        return qcError(u"Corrupt content: item table"_s);
    }
    for (quint32 i = 0; i < itemCount; ++i) {
        QcItemDefinition item;
        qint32 maxStack = 0;
        qint32 toolLevel = 0;
        qint32 durability = 0;
        stream >> item.name >> item.translationKey >> item.texture >> maxStack >> item.placesBlock >> item.tool >> toolLevel
            >> item.toolSpeed >> durability >> item.attackDamage;
        item.maxStack = maxStack;
        item.toolLevel = toolLevel;
        item.durability = durability;
        if (stream.status() != QDataStream::Ok || !registry.registerItem(std::move(item))) {
            return qcError(u"Corrupt content: item %1"_s.arg(i));
        }
    }
    if (registry.itemCount() != qsizetype(itemCount)) {
        return qcError(u"Corrupt content: duplicate item names"_s);
    }

    quint32 mobCount = 0;
    if (!QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions, mobCount)) {
        return qcError(u"Corrupt content: mob table"_s);
    }
    for (quint32 i = 0; i < mobCount; ++i) {
        QcMobDefinition mob;
        quint32 partCount = 0;
        stream >> mob.name >> mob.translationKey >> mob.maxHealth >> mob.width >> mob.height >> mob.hostile;
        if (!QcStreamLimits::readCount(stream, 256, partCount)) {
            return qcError(u"Corrupt content: mob %1"_s.arg(i));
        }
        for (quint32 p = 0; p < partCount; ++p) {
            QcMobPart part;
            stream >> part.from.x >> part.from.y >> part.from.z >> part.to.x >> part.to.y >> part.to.z >> part.texture;
            mob.parts.append(part);
        }
        // Sizes feed physics and the mesher: reject anything outside what the parser allows.
        const auto plausible = [](double value) { return std::isfinite(value) && std::abs(value) <= 64.0; };
        bool valid = std::isfinite(mob.maxHealth) && mob.maxHealth > 0.0 && mob.maxHealth <= 1000.0 && plausible(mob.width) && plausible(mob.height) && mob.width > 0.0
                     && mob.height > 0.0;
        for (const QcMobPart &part : std::as_const(mob.parts)) {
            valid = valid && plausible(part.from.x) && plausible(part.from.y) && plausible(part.from.z)
                    && plausible(part.to.x) && plausible(part.to.y) && plausible(part.to.z);
        }
        if (stream.status() != QDataStream::Ok || !valid) {
            return qcError(u"Corrupt content: mob %1"_s.arg(i));
        }
        registry.registerMob(std::move(mob));
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

#include "QcContentRegistry.h"

#include <cmath>

#include "QcContentNames.h"
#include "QcPartGenerator.h"
#include "QcStreamLimits.h"

#include <QJsonDocument>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

bool plausibleFuel(double seconds)
{
    return std::isfinite(seconds) && seconds >= 0.0 && seconds <= 100000.0;
}

void writeAabbs(QDataStream &stream, const QList<QcAabb> &boxes)
{
    stream << quint32(boxes.size());
    for (const QcAabb &box : boxes) {
        stream << box.min.x << box.min.y << box.min.z << box.max.x << box.max.y << box.max.z;
    }
}

bool readAabbs(QDataStream &stream, QList<QcAabb> &boxes, double maxY)
{
    quint32 count = 0;
    if (!QcStreamLimits::readCount(stream, QcShapeDefinition::maxBoxesPerPart, count)) {
        return false;
    }
    for (quint32 i = 0; i < count; ++i) {
        QcAabb box;
        stream >> box.min.x >> box.min.y >> box.min.z >> box.max.x >> box.max.y >> box.max.z;
        // Boxes feed collision, raycasts and the mesher: they must lie within their cell.
        const bool inside = box.min.x >= 0.0 && box.max.x <= 1.0 && box.min.z >= 0.0 && box.max.z <= 1.0
                            && box.min.y >= 0.0 && box.max.y <= maxY;
        if (stream.status() != QDataStream::Ok || !inside || !(box.min.x < box.max.x) || !(box.min.y < box.max.y)
            || !(box.min.z < box.max.z)) {
            stream.setStatus(QDataStream::ReadCorruptData);
            return false;
        }
        boxes.append(box);
    }
    return true;
}

void writeShape(QDataStream &stream, const QcShapeDefinition &shape)
{
    stream << shape.name << shape.rotate << shape.flip << quint32(shape.parts.size());
    for (const QcShapePart &part : shape.parts) {
        stream << quint32(part.when.size());
        for (const QcShapeCondition &condition : part.when) {
            stream << condition.property << quint32(condition.values.size());
            for (const QString &value : condition.values) {
                stream << value;
            }
        }
        writeAabbs(stream, part.boxes);
        writeAabbs(stream, part.crosses);
        stream << part.customCollision;
        writeAabbs(stream, part.collision);
        stream << part.texture << part.stretch;
    }
}

bool readShape(QDataStream &stream, QcShapeDefinition &shape)
{
    quint32 partCount = 0;
    stream >> shape.name >> shape.rotate >> shape.flip;
    if (!QcStreamLimits::readCount(stream, QcShapeDefinition::maxParts, partCount)) {
        return false;
    }
    for (quint32 p = 0; p < partCount; ++p) {
        QcShapePart part;
        quint32 conditionCount = 0;
        if (!QcStreamLimits::readCount(stream, QcShapeDefinition::maxConditions, conditionCount)) {
            return false;
        }
        for (quint32 c = 0; c < conditionCount; ++c) {
            QcShapeCondition condition;
            quint32 valueCount = 0;
            stream >> condition.property;
            if (!QcStreamLimits::readCount(stream, QcBlockProperty::maxValues, valueCount)) {
                return false;
            }
            for (quint32 v = 0; v < valueCount; ++v) {
                QString value;
                stream >> value;
                condition.values.append(value);
            }
            part.when.append(std::move(condition));
        }
        if (!readAabbs(stream, part.boxes, 1.0) || !readAabbs(stream, part.crosses, 1.0)) {
            return false;
        }
        stream >> part.customCollision;
        if (!readAabbs(stream, part.collision, QcShapeDefinition::maxCollisionHeight)) {
            return false;
        }
        stream >> part.texture >> part.stretch;
        shape.parts.append(std::move(part));
    }
    return stream.status() == QDataStream::Ok && shape.boxCount() <= QcShapeDefinition::maxBoxes;
}

void writeBlock(QDataStream &stream, const QcBlockDefinition &block, const QcShapeDefinition &shape)
{
    stream << block.name << block.translationKey << quint8(block.renderLayer) << block.solid << block.opaque
           << block.liquid << block.replaceable << qint32(block.lightOpacity) << block.hardness << block.tool
           << qint32(block.toolLevel) << block.opensCrafting << block.machine.category
           << block.machine.speed << block.fuelSeconds << block.machine.sound << block.soundMaterial;
    for (const QString &key : block.soundKeys) {
        stream << key;
    }
    stream << quint32(block.drops.size());
    for (const QcItemDrop &drop : block.drops) {
        stream << drop.item << qint32(drop.count) << drop.chance;
    }
    const QcSaplingDefinition &sapling = block.sapling;
    stream << sapling.feature << sapling.minSeconds << sapling.maxSeconds << sapling.plantChance
           << quint32(sapling.plantOn.size());
    for (const QString &ground : sapling.plantOn) {
        stream << ground;
    }
    for (const QString &texture : block.faceTextures) {
        stream << texture;
    }
    stream << quint32(block.properties.size());
    for (const QcBlockProperty &property : block.properties) {
        stream << property.name << quint32(property.values.size());
        for (const QString &value : property.values) {
            stream << value;
        }
    }
    writeShape(stream, shape);
    stream << quint32(block.textureSlots.size());
    for (const auto &[slot, texture] : block.textureSlots) {
        stream << slot << texture;
    }
    stream << block.itemTexture << qint32(block.lightLevel) << block.climbable << block.toggles << block.door
           << quint8(block.facingRule) << quint8(block.support) << block.connectGroup << quint32(block.connectsTo.size());
    for (const QString &group : block.connectsTo) {
        stream << group;
    }
    stream << qint32(block.containerSlots);
}

bool readBlock(QDataStream &stream, QcBlockDefinition &block)
{
    quint8 renderLayer = 0;
    qint32 lightOpacity = 0;
    qint32 toolLevel = 0;
    stream >> block.name >> block.translationKey >> renderLayer >> block.solid >> block.opaque >> block.liquid
        >> block.replaceable >> lightOpacity >> block.hardness >> block.tool >> toolLevel >> block.opensCrafting
        >> block.machine.category >> block.machine.speed >> block.fuelSeconds >> block.machine.sound >> block.soundMaterial;
    for (QString &key : block.soundKeys) {
        stream >> key;
    }
    // Machine speed and fuel time drive server-side timers; they must stay within the parser's ranges.
    const bool machineOk = block.machine.category.size() <= 64 && std::isfinite(block.machine.speed)
                           && block.machine.speed > 0.0 && block.machine.speed <= 100.0;
    if (renderLayer > quint8(QcRenderLayer::Translucent) || lightOpacity < 0 || lightOpacity > 15
        || !machineOk || !plausibleFuel(block.fuelSeconds)) {
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
        stream >> drop.item >> count >> drop.chance;
        drop.count = count;
        block.drops.append(drop);
    }
    QcSaplingDefinition &sapling = block.sapling;
    quint32 groundCount = 0;
    stream >> sapling.feature >> sapling.minSeconds >> sapling.maxSeconds >> sapling.plantChance;
    if (!QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions, groundCount)) {
        return false;
    }
    sapling.plantOn.clear();
    for (quint32 i = 0; i < groundCount; ++i) {
        QString ground;
        stream >> ground;
        sapling.plantOn.append(ground);
    }
    if (!std::isfinite(sapling.minSeconds) || !std::isfinite(sapling.maxSeconds) || !std::isfinite(sapling.plantChance)) {
        stream.setStatus(QDataStream::ReadCorruptData);
        return false;
    }
    for (QString &texture : block.faceTextures) {
        stream >> texture;
    }
    quint32 propertyCount = 0;
    if (!QcStreamLimits::readCount(stream, QcBlockProperty::maxProperties, propertyCount)) {
        return false;
    }
    for (quint32 i = 0; i < propertyCount; ++i) {
        QcBlockProperty property;
        quint32 valueCount = 0;
        stream >> property.name;
        if (!QcStreamLimits::readCount(stream, QcBlockProperty::maxValues, valueCount)) {
            return false;
        }
        for (quint32 v = 0; v < valueCount; ++v) {
            QString value;
            stream >> value;
            property.values.append(value);
        }
        block.properties.append(std::move(property));
    }
    if (!readShape(stream, block.shape)) {
        return false;
    }
    quint32 slotCount = 0;
    if (!QcStreamLimits::readCount(stream, 16, slotCount)) {
        return false;
    }
    for (quint32 i = 0; i < slotCount; ++i) {
        QString slot;
        QString texture;
        stream >> slot >> texture;
        block.textureSlots.append({slot, texture});
    }
    qint32 lightLevel = 0;
    quint8 facingRule = 0;
    quint8 support = 0;
    quint32 groupCount = 0;
    stream >> block.itemTexture >> lightLevel >> block.climbable >> block.toggles >> block.door >> facingRule >> support
        >> block.connectGroup;
    if (!QcStreamLimits::readCount(stream, 16, groupCount)) {
        return false;
    }
    for (quint32 i = 0; i < groupCount; ++i) {
        QString group;
        stream >> group;
        block.connectsTo.append(group);
    }
    qint32 containerSlots = 0;
    stream >> containerSlots;
    if (stream.status() != QDataStream::Ok || lightLevel < 0 || lightLevel > QcBlockDefinition::maxLightLevel
        || facingRule > quint8(QcFacingRule::Toward) || support > quint8(QcBlockSupport::Attached) || containerSlots < 0
        || containerSlots > QcBlockDefinition::maxContainerSlots || !QcBlockStateTable::problem(block).isEmpty()) {
        stream.setStatus(QDataStream::ReadCorruptData);
        return false;
    }
    block.lightLevel = lightLevel;
    block.facingRule = QcFacingRule(facingRule);
    block.support = QcBlockSupport(support);
    block.containerSlots = containerSlots;
    return stream.status() == QDataStream::Ok;
}

// Model coordinates feed the mesher: anything beyond what the parser allows is corrupt.
bool plausibleVector(const QcVec3 &v)
{
    constexpr double limit = 8.0;
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::abs(v.x) <= limit && std::abs(v.y) <= limit
           && std::abs(v.z) <= limit;
}

void writeBoxes(QDataStream &stream, const QList<QcModelBox> &boxes)
{
    stream << quint32(boxes.size());
    for (const QcModelBox &box : boxes) {
        stream << box.from.x << box.from.y << box.from.z << box.to.x << box.to.y << box.to.z << box.slot;
    }
}

bool readBoxes(QDataStream &stream, quint32 limit, QList<QcModelBox> &boxes)
{
    quint32 count = 0;
    if (!QcStreamLimits::readCount(stream, limit, count)) {
        return false;
    }
    for (quint32 i = 0; i < count; ++i) {
        QcModelBox box;
        stream >> box.from.x >> box.from.y >> box.from.z >> box.to.x >> box.to.y >> box.to.z >> box.slot;
        if (stream.status() != QDataStream::Ok || !plausibleVector(box.from) || !plausibleVector(box.to)) {
            return false;
        }
        boxes.append(std::move(box));
    }
    return true;
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
    m_blockIds.insert(air.name, 0);
    m_states.append(m_blocks.constFirst(), {});
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
    if (const QString problem = QcBlockStateTable::problem(definition); !problem.isEmpty()) {
        return qcError(u"Block '%1': %2"_s.arg(definition.name, problem));
    }
    const qsizetype states = QcBlockStateTable::stateCountOf(definition);
    const auto existing = m_blockIds.constFind(definition.name);
    const qsizetype boxes = QcBlockStateTable::derivedBoxes(definition, shapeOf(definition));
    if (!m_states.fits(existing != m_blockIds.constEnd() ? std::optional<quint32>(*existing) : std::nullopt, boxes)) {
        return qcError(u"Block '%1': its shape variants need %2 boxes, more than the %3 left for all block shapes together"_s
                           .arg(definition.name)
                           .arg(boxes)
                           .arg(QcBlockStateTable::maxDerivedBoxes - m_states.totalDerivedBoxes()));
    }
    if (existing != m_blockIds.constEnd()) {
        const quint32 type = *existing;
        const int previous = m_states.stateCount(type);
        if (m_states.size() - previous + states > QcBlockStateTable::maxStates) {
            return qcError(u"Block '%1': too many block states in total"_s.arg(definition.name));
        }
        m_blocks[type] = std::move(definition);
        if (states == previous) {
            m_states.update(type, m_blocks[type], shapeOf(m_blocks[type]));
        } else {
            rebuildStates();
        }
        return m_states.firstState(type);
    }
    if (m_states.size() + states > QcBlockStateTable::maxStates) {
        return qcError(u"Block '%1': too many block states in total"_s.arg(definition.name));
    }
    const auto type = quint32(m_blocks.size());
    m_blockIds.insert(definition.name, type);
    m_blocks.append(std::move(definition));
    m_states.append(m_blocks.constLast(), shapeOf(m_blocks.constLast()));
    return m_states.firstState(type);
}

void QcContentRegistry::rebuildStates()
{
    m_states.clear();
    for (const QcBlockDefinition &block : std::as_const(m_blocks)) {
        m_states.append(block, shapeOf(block));
    }
}

QcStatus QcContentRegistry::registerShape(QcShapeDefinition shape)
{
    const QString name = shape.name;
    if (shape.boxCount() > QcShapeDefinition::maxBoxes) {
        return qcError(u"Shape '%1': more than %2 boxes"_s.arg(name).arg(QcShapeDefinition::maxBoxes));
    }
    qsizetype total = m_states.totalDerivedBoxes();
    for (const QcBlockDefinition &block : std::as_const(m_blocks)) {
        if (block.shapeName == name) {
            total += QcBlockStateTable::derivedBoxes(block, shape) - QcBlockStateTable::derivedBoxes(block, shapeOf(block));
        }
    }
    if (total > QcBlockStateTable::maxDerivedBoxes) {
        return qcError(u"Shape '%1': the blocks using it would need more than %2 boxes for all block shapes together"_s.arg(name).arg(
            QcBlockStateTable::maxDerivedBoxes));
    }
    const auto existing = m_shapeIndex.constFind(name);
    if (existing != m_shapeIndex.constEnd()) {
        m_shapes[*existing] = std::move(shape);
    } else {
        m_shapeIndex.insert(name, m_shapes.size());
        m_shapes.append(std::move(shape));
    }
    for (qsizetype type = 0; type < m_blocks.size(); ++type) {
        if (m_blocks[type].shapeName == name) {
            m_states.update(quint32(type), m_blocks[type], shapeOf(m_blocks[type]));
        }
    }
    return {};
}

const QcShapeDefinition *QcContentRegistry::shape(const QString &name) const
{
    const auto it = m_shapeIndex.constFind(name);
    return it == m_shapeIndex.constEnd() ? nullptr : &m_shapes[*it];
}

const QcShapeDefinition &QcContentRegistry::shapeOf(const QcBlockDefinition &block) const
{
    if (!block.shapeName.isEmpty()) {
        if (const QcShapeDefinition *named = shape(block.shapeName)) {
            return *named;
        }
    }
    return block.shape;
}

const QcBlockState &QcContentRegistry::state(QcBlockId id) const
{
    Q_ASSERT(isValidBlock(id));
    return m_states.state(id);
}

QString QcContentRegistry::property(QcBlockId id, const QString &property) const
{
    const QcBlockState &info = state(id);
    return QcBlockStateTable::value(m_blocks[qsizetype(info.type)], info.index, property);
}

QcBlockId QcContentRegistry::withProperty(QcBlockId id, const QString &property, const QString &value) const
{
    const QcBlockState &info = state(id);
    const std::optional<quint32> index = QcBlockStateTable::withValue(m_blocks[qsizetype(info.type)], info.index, property, value);
    return index ? m_states.firstState(info.type) + *index : id;
}

QString QcContentRegistry::stateName(QcBlockId id) const
{
    const QcBlockState &info = state(id);
    const QcBlockDefinition &definition = m_blocks[qsizetype(info.type)];
    return definition.name + QcBlockStateTable::describe(definition, info.index);
}

std::optional<QcBlockId> QcContentRegistry::stateId(const QString &text, QStringList *problems) const
{
    const qsizetype open = text.indexOf(u'[');
    const QString name = open < 0 ? text : text.left(open);
    const auto type = m_blockIds.constFind(name);
    if (type == m_blockIds.constEnd()) {
        return std::nullopt;
    }
    QcBlockId id = m_states.firstState(*type);
    if (open < 0) {
        return id;
    }
    const bool closed = text.endsWith(u']');
    if (!closed && problems) {
        problems->append(u"%1: unterminated state '%2'"_s.arg(name, text.mid(open)));
    }
    const QStringList assignments = text.mid(open + 1, text.size() - open - (closed ? 2 : 1)).split(u',', Qt::SkipEmptyParts);
    for (const QString &assignment : assignments) {
        const qsizetype equals = assignment.indexOf(u'=');
        const QString property = assignment.left(equals);
        const QString value = equals < 0 ? QString() : assignment.mid(equals + 1);
        const QcBlockId changed = withProperty(id, property, value);
        if (changed == id && QcBlockStateTable::value(m_blocks[qsizetype(*type)], state(id).index, property) != value) {
            if (problems) {
                problems->append(u"%1: unknown state '%2'"_s.arg(name, assignment));
            }
            continue;
        }
        id = changed;
    }
    return id;
}

QcResult<QcItemId> QcContentRegistry::registerItem(QcItemDefinition definition)
{
    if (!QcContentNames::isValidQualified(definition.name)) {
        return qcError(u"Invalid item name '%1'"_s.arg(definition.name));
    }
    if (definition.maxStack < 1 || definition.maxStack > QcItemDefinition::maxStackLimit) {
        return qcError(u"Item '%1': maxStack must be within 1..%2"_s.arg(definition.name).arg(QcItemDefinition::maxStackLimit));
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
        item.fuelSeconds = block.fuelSeconds;
        item.tags = block.itemTags;
        if (!block.itemTexture.isEmpty()) {
            item.texture = block.itemTexture;
        }
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

// Replaces the entry with the same name or appends; `index` maps names to positions in `list`.
template<typename Definition>
void registerByName(QList<Definition> &list, QHash<QString, qsizetype> &index, Definition definition)
{
    const auto existing = index.constFind(definition.name);
    if (existing != index.constEnd()) {
        list[*existing] = std::move(definition);
    } else {
        index.insert(definition.name, list.size());
        list.append(std::move(definition));
    }
}

} // namespace

void QcContentRegistry::registerPartFamily(QcPartFamily family)
{
    registerByName(m_partFamilies, m_partFamilyIndex, std::move(family));
}

void QcContentRegistry::registerMaterial(QcMaterialDefinition material)
{
    registerByName(m_materials, m_materialIndex, std::move(material));
}

QStringList QcContentRegistry::finish()
{
    QStringList warnings = QcPartGenerator::generate(*this);
    createBlockItems();
    return warnings;
}

void QcContentRegistry::registerMob(QcMobDefinition definition)
{
    registerByName(m_mobs, m_mobIndex, std::move(definition));
}

std::optional<int> QcContentRegistry::mobIndex(const QString &name) const
{
    const auto it = m_mobIndex.constFind(name);
    if (it == m_mobIndex.constEnd()) {
        return std::nullopt;
    }
    return int(*it);
}

void QcContentRegistry::registerModel(QcModelDefinition definition)
{
    registerByName(m_models, m_modelIndex, std::move(definition));
}

const QcModelDefinition *QcContentRegistry::model(const QString &name) const
{
    const auto it = m_modelIndex.constFind(name);
    return it == m_modelIndex.constEnd() ? nullptr : &m_models[*it];
}

void QcContentRegistry::registerAppearance(QcAppearanceDefinition definition)
{
    registerByName(m_appearances, m_appearanceIndex, std::move(definition));
}

const QcAppearanceDefinition *QcContentRegistry::appearance(const QString &name) const
{
    const auto it = m_appearanceIndex.constFind(name);
    return it == m_appearanceIndex.constEnd() ? nullptr : &m_appearances[*it];
}

void QcContentRegistry::registerRecipe(QcRecipeDefinition definition)
{
    registerByName(m_recipes, m_recipeIndex, std::move(definition));
}

std::optional<qsizetype> QcContentRegistry::recipeIndex(const QString &name) const
{
    const auto it = m_recipeIndex.constFind(name);
    if (it == m_recipeIndex.constEnd()) {
        return std::nullopt;
    }
    return *it;
}

void QcContentRegistry::registerSmeltingRecipe(QcSmeltingRecipe recipe)
{
    registerByName(m_smeltingRecipes, m_smeltingIndex, std::move(recipe));
}

const QcSmeltingRecipe *QcContentRegistry::smeltingRecipe(const QString &category, const QString &inputItem) const
{
    const auto it = std::ranges::find_if(m_smeltingRecipes, [&](const QcSmeltingRecipe &recipe) {
        return recipe.inputItem == inputItem && recipe.category == category;
    });
    return it == m_smeltingRecipes.end() ? nullptr : &*it;
}

void QcContentRegistry::registerSound(QcSoundDefinition definition)
{
    registerByName(m_sounds, m_soundIndex, std::move(definition));
}

const QcSoundDefinition *QcContentRegistry::sound(const QString &name) const
{
    const auto it = m_soundIndex.constFind(name);
    return it == m_soundIndex.constEnd() ? nullptr : &m_sounds[*it];
}

void QcContentRegistry::registerBiome(QcBiomeDefinition definition)
{
    registerByName(m_biomes, m_biomeIndex, std::move(definition));
}

void QcContentRegistry::registerFeature(QcFeatureDefinition definition)
{
    registerByName(m_features, m_featureIndex, std::move(definition));
}

void QcContentRegistry::registerOre(QcOreDefinition definition)
{
    registerByName(m_ores, m_oreIndex, std::move(definition));
}

const QcBlockDefinition &QcContentRegistry::block(QcBlockId id) const
{
    Q_ASSERT(isValidBlock(id));
    return m_blocks[qsizetype(m_states.state(id).type)];
}

std::optional<QcBlockId> QcContentRegistry::blockId(const QString &name) const
{
    const auto it = m_blockIds.constFind(name);
    if (it == m_blockIds.constEnd()) {
        return std::nullopt;
    }
    return m_states.firstState(*it);
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

bool QcContentRegistry::hasTranslation(const QString &key, const QString &language) const
{
    const auto table = m_translations.constFind(language);
    return table != m_translations.constEnd() && table->contains(key);
}

void QcContentRegistry::writeTo(QDataStream &stream) const
{
    stream << quint32(m_blocks.size());
    for (const QcBlockDefinition &block : m_blocks) {
        writeBlock(stream, block, shapeOf(block));
    }
    stream << quint32(m_items.size());
    for (const QcItemDefinition &item : m_items) {
        stream << item.name << item.translationKey << item.texture << qint32(item.maxStack) << item.placesBlock << item.tool
               << qint32(item.toolLevel) << item.toolSpeed << qint32(item.durability) << item.attackDamage << qint32(item.food)
               << item.fuelSeconds << quint32(item.tags.size());
        for (const QString &tag : item.tags) {
            stream << tag;
        }
    }
    stream << quint32(m_mobs.size());
    for (const QcMobDefinition &mob : m_mobs) {
        stream << mob.name << mob.translationKey << mob.maxHealth << mob.width << mob.height << mob.hostile
               << quint32(mob.parts.size());
        for (const QcMobPart &part : mob.parts) {
            stream << part.from.x << part.from.y << part.from.z << part.to.x << part.to.y << part.to.z << part.texture;
        }
        stream << mob.skeleton << mob.outfit << mob.armsForward << mob.ambientSound << mob.hurtSound << mob.deathSound;
    }
    stream << quint32(m_recipes.size());
    for (const QcRecipeDefinition &recipe : m_recipes) {
        stream << recipe.name << recipe.shaped << quint32(recipe.pattern.size());
        for (const QString &row : recipe.pattern) {
            stream << row;
        }
        stream << quint32(recipe.keys.size());
        for (const auto &[key, item] : recipe.keys) {
            stream << key << item;
        }
        stream << quint32(recipe.ingredients.size());
        for (const QString &ingredient : recipe.ingredients) {
            stream << ingredient;
        }
        stream << recipe.resultItem << qint32(recipe.resultCount);
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
    stream << quint32(m_packs.size());
    for (const QcPackReference &pack : m_packs) {
        stream << pack.id << pack.name << pack.version;
    }
    stream << quint32(m_models.size());
    for (const QcModelDefinition &model : m_models) {
        stream << model.name << quint32(model.bones.size());
        for (const QcModelBone &bone : model.bones) {
            stream << bone.name << quint8(bone.role) << bone.pivot.x << bone.pivot.y << bone.pivot.z;
            writeBoxes(stream, bone.boxes);
        }
    }
    stream << quint32(m_appearances.size());
    for (const QcAppearanceDefinition &appearance : m_appearances) {
        stream << appearance.name << appearance.translationKey << quint8(appearance.kind) << appearance.playable
               << quint32(appearance.layers.size());
        for (const QcAppearanceLayer &layer : appearance.layers) {
            stream << layer.slot << layer.texture << quint8(layer.tint);
        }
        writeBoxes(stream, appearance.boxes);
    }
    stream << quint32(m_smeltingRecipes.size());
    for (const QcSmeltingRecipe &recipe : m_smeltingRecipes) {
        stream << recipe.name << recipe.category << recipe.inputItem << recipe.resultItem << qint32(recipe.resultCount)
               << recipe.seconds;
    }
    stream << quint32(m_sounds.size());
    for (const QcSoundDefinition &sound : m_sounds) {
        stream << sound.name << quint8(sound.bus) << sound.volume << sound.pitchVariation << qint32(sound.variants) << sound.loop
               << sound.range << quint32(sound.layers.size());
        for (const QcSoundLayer &layer : sound.layers) {
            stream << quint8(layer.wave) << layer.frequency << layer.endFrequency << layer.start << layer.duration << layer.attack
                   << layer.curve << layer.gain << layer.lowpass << layer.highpass << layer.tremoloRate << layer.tremoloDepth
                   << layer.density;
        }
    }
}

QcResult<QcContentRegistry> QcContentRegistry::readFrom(QDataStream &stream)
{
    QcContentRegistry registry;
    registry.m_blocks.clear();
    registry.m_blockIds.clear();
    registry.m_states.clear();

    quint32 blockCount = 0;
    if (!QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions, blockCount) || blockCount == 0) {
        return qcError(u"Corrupt content: block table"_s);
    }
    for (quint32 i = 0; i < blockCount; ++i) {
        QcBlockDefinition block;
        if (!readBlock(stream, block) || registry.m_blockIds.contains(block.name)
            || registry.m_states.size() + QcBlockStateTable::stateCountOf(block) > QcBlockStateTable::maxStates
            || !registry.m_states.fits(std::nullopt, QcBlockStateTable::derivedBoxes(block, block.shape))) {
            return qcError(u"Corrupt content: block %1"_s.arg(i));
        }
        // The sender resolved the shape; the receiver has no shape table of its own.
        block.shapeName.clear();
        registry.m_blockIds.insert(block.name, i);
        registry.m_blocks.append(std::move(block));
        registry.m_states.append(registry.m_blocks.constLast(), registry.m_blocks.constLast().shape);
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
        qint32 food = 0;
        quint32 tagCount = 0;
        stream >> item.name >> item.translationKey >> item.texture >> maxStack >> item.placesBlock >> item.tool >> toolLevel
            >> item.toolSpeed >> durability >> item.attackDamage >> food >> item.fuelSeconds;
        if (!QcStreamLimits::readCount(stream, 16, tagCount)) {
            return qcError(u"Corrupt content: item %1"_s.arg(i));
        }
        for (quint32 t = 0; t < tagCount; ++t) {
            QString tag;
            stream >> tag;
            item.tags.append(tag);
        }
        item.maxStack = maxStack;
        item.toolLevel = toolLevel;
        item.durability = durability;
        item.food = food;
        if (stream.status() != QDataStream::Ok || food < 0 || food > QcItemDefinition::maxFood || !plausibleFuel(item.fuelSeconds)
            || !registry.registerItem(std::move(item))) {
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
        stream >> mob.skeleton >> mob.outfit >> mob.armsForward >> mob.ambientSound >> mob.hurtSound >> mob.deathSound;
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

    quint32 recipeCount = 0;
    if (!QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions, recipeCount)) {
        return qcError(u"Corrupt content: recipe table"_s);
    }
    for (quint32 i = 0; i < recipeCount; ++i) {
        QcRecipeDefinition recipe;
        quint32 keyCount = 0;
        qint32 resultCount = 0;
        stream >> recipe.name >> recipe.shaped;
        // Bounded reads: QDataStream's own list operator would reserve whatever count arrives.
        const auto readStrings = [&stream](QList<QString> &list) {
            quint32 count = 0;
            if (!QcStreamLimits::readCount(stream, 9, count)) {
                return false;
            }
            for (quint32 n = 0; n < count; ++n) {
                QString text;
                stream >> text;
                list.append(text);
            }
            return true;
        };
        if (!readStrings(recipe.pattern) || !QcStreamLimits::readCount(stream, 9, keyCount)) {
            return qcError(u"Corrupt content: recipe %1"_s.arg(i));
        }
        for (quint32 k = 0; k < keyCount; ++k) {
            QChar key;
            QString item;
            stream >> key >> item;
            recipe.keys.append({key, item});
        }
        if (!readStrings(recipe.ingredients)) {
            return qcError(u"Corrupt content: recipe %1"_s.arg(i));
        }
        stream >> recipe.resultItem >> resultCount;
        recipe.resultCount = resultCount;
        // Patterns must stay within a 3 x 3 grid and be rectangular, whatever the sender claims.
        const bool rectangular = std::ranges::all_of(recipe.pattern, [&recipe](const QString &row) {
            return row.size() == recipe.pattern.first().size() && row.size() <= 3;
        });
        if (stream.status() != QDataStream::Ok || recipe.pattern.size() > 3 || (recipe.shaped && (recipe.pattern.isEmpty() || !rectangular))
            || (!recipe.shaped && recipe.ingredients.isEmpty()) || recipe.ingredients.size() > 9 || resultCount < 1) {
            return qcError(u"Corrupt content: recipe %1"_s.arg(i));
        }
        registry.registerRecipe(std::move(recipe));
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
    quint32 packCount = 0;
    if (!QcStreamLimits::readCount(stream, 1024, packCount)) {
        return qcError(u"Corrupt content: pack list"_s);
    }
    for (quint32 i = 0; i < packCount; ++i) {
        QcPackReference pack;
        stream >> pack.id >> pack.name >> pack.version;
        if (pack.id.isEmpty() || pack.id.size() > 64 || pack.name.size() > 256 || pack.version.size() > 64) {
            return qcError(u"Corrupt content: pack %1"_s.arg(i));
        }
        registry.addPack(std::move(pack));
    }
    quint32 modelCount = 0;
    if (!QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions, modelCount)) {
        return qcError(u"Corrupt content: model table"_s);
    }
    for (quint32 i = 0; i < modelCount; ++i) {
        QcModelDefinition model;
        quint32 boneCount = 0;
        stream >> model.name;
        if (!QcStreamLimits::readCount(stream, QcModelDefinition::maxBones, boneCount)) {
            return qcError(u"Corrupt content: model %1"_s.arg(i));
        }
        for (quint32 b = 0; b < boneCount; ++b) {
            QcModelBone bone;
            quint8 role = 0;
            stream >> bone.name >> role >> bone.pivot.x >> bone.pivot.y >> bone.pivot.z;
            if (role > quint8(QcBoneRole::LeftLeg) || !plausibleVector(bone.pivot)
                || !readBoxes(stream, QcModelDefinition::maxBoxesPerBone, bone.boxes)) {
                return qcError(u"Corrupt content: model %1"_s.arg(i));
            }
            bone.role = QcBoneRole(role);
            model.bones.append(std::move(bone));
        }
        if (stream.status() != QDataStream::Ok) {
            return qcError(u"Corrupt content: model %1"_s.arg(i));
        }
        registry.registerModel(std::move(model));
    }
    quint32 appearanceCount = 0;
    if (!QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions, appearanceCount)) {
        return qcError(u"Corrupt content: appearance table"_s);
    }
    for (quint32 i = 0; i < appearanceCount; ++i) {
        QcAppearanceDefinition appearance;
        quint8 kind = 0;
        quint32 layerCount = 0;
        stream >> appearance.name >> appearance.translationKey >> kind >> appearance.playable;
        if (kind > quint8(QcAppearanceDefinition::Kind::Hair)
            || !QcStreamLimits::readCount(stream, QcAppearanceDefinition::maxLayers, layerCount)) {
            return qcError(u"Corrupt content: appearance %1"_s.arg(i));
        }
        appearance.kind = QcAppearanceDefinition::Kind(kind);
        for (quint32 l = 0; l < layerCount; ++l) {
            QcAppearanceLayer layer;
            quint8 tint = 0;
            stream >> layer.slot >> layer.texture >> tint;
            if (tint > quint8(QcTint::Pants)) {
                return qcError(u"Corrupt content: appearance %1"_s.arg(i));
            }
            layer.tint = QcTint(tint);
            appearance.layers.append(std::move(layer));
        }
        if (!readBoxes(stream, QcAppearanceDefinition::maxBoxes, appearance.boxes) || stream.status() != QDataStream::Ok) {
            return qcError(u"Corrupt content: appearance %1"_s.arg(i));
        }
        registry.registerAppearance(std::move(appearance));
    }
    quint32 smeltingCount = 0;
    if (!QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions, smeltingCount)) {
        return qcError(u"Corrupt content: smelting recipes"_s);
    }
    for (quint32 i = 0; i < smeltingCount; ++i) {
        QcSmeltingRecipe recipe;
        qint32 resultCount = 0;
        stream >> recipe.name >> recipe.category >> recipe.inputItem >> recipe.resultItem >> resultCount >> recipe.seconds;
        recipe.resultCount = resultCount;
        if (stream.status() != QDataStream::Ok || resultCount < 1 || resultCount > 64 || !std::isfinite(recipe.seconds)
            || recipe.seconds < QcSmeltingRecipe::minSeconds || recipe.seconds > QcSmeltingRecipe::maxSeconds) {
            return qcError(u"Corrupt content: smelting recipe %1"_s.arg(i));
        }
        registry.registerSmeltingRecipe(std::move(recipe));
    }
    quint32 soundCount = 0;
    if (!QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions, soundCount)) {
        return qcError(u"Corrupt content: sound table"_s);
    }
    for (quint32 i = 0; i < soundCount; ++i) {
        QcSoundDefinition sound;
        quint8 bus = 0;
        qint32 variants = 0;
        quint32 layerCount = 0;
        stream >> sound.name >> bus >> sound.volume >> sound.pitchVariation >> variants >> sound.loop >> sound.range;
        if (!QcStreamLimits::readCount(stream, QcSoundDefinition::maxLayers, layerCount)) {
            return qcError(u"Corrupt content: sound %1"_s.arg(i));
        }
        sound.bus = QcSoundBus(bus);
        sound.variants = variants;
        for (quint32 l = 0; l < layerCount; ++l) {
            QcSoundLayer layer;
            quint8 wave = 0;
            stream >> wave >> layer.frequency >> layer.endFrequency >> layer.start >> layer.duration >> layer.attack >> layer.curve
                >> layer.gain >> layer.lowpass >> layer.highpass >> layer.tremoloRate >> layer.tremoloDepth >> layer.density;
            layer.wave = QcSoundWave(wave);
            sound.layers.append(layer);
        }
        // The synthesizer's work and memory follow from these values: the same limits as for content files.
        if (stream.status() != QDataStream::Ok || !sound.problem().isEmpty()) {
            return qcError(u"Corrupt content: sound %1"_s.arg(i));
        }
        registry.registerSound(std::move(sound));
    }
    if (stream.status() != QDataStream::Ok) {
        return qcError(u"Corrupt content: truncated"_s);
    }
    return registry;
}

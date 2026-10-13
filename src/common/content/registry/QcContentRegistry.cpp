#include "QcContentRegistry.h"

#include "QcContentNames.h"
#include "QcContentSerializer.h"
#include "QcPartGenerator.h"
#include "QcSignalLinks.h"

#include <algorithm>

using namespace Qt::StringLiterals;

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
        if (block.renderLayer == QcRenderLayer::Invisible || block.liquid || !block.hasItem || m_itemIds.contains(block.name)) {
            continue;
        }

        QcItemDefinition item;
        item.name = block.name;
        item.translationKey = block.translationKey;
        item.descriptionKey = block.descriptionKey;
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
    // A circuit block with a broken link (a mod's piston without a valid head) takes no part in
    // circuits rather than misbehave (decision E-333).
    const auto problems = QcSignalLinks::problems(*this);
    for (const auto &[name, problem] : std::as_const(problems)) {
        const std::optional<QcBlockId> id = blockId(name);
        if (!id) {
            continue;
        }
        m_blocks[qsizetype(m_states.state(*id).type)].signal.kind = QcSignalKind::None;
        warnings.append(u"block '%1': %2; it takes no part in circuits"_s.arg(name, problem));
    }
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

void QcContentRegistry::registerHandbookChapter(QcHandbookChapter chapter)
{
    registerByName(m_handbookChapters, m_handbookChapterIndex, std::move(chapter));
}

void QcContentRegistry::registerFeature(QcFeatureDefinition definition)
{
    registerByName(m_features, m_featureIndex, std::move(definition));
}

void QcContentRegistry::registerOre(QcOreDefinition definition)
{
    registerByName(m_ores, m_oreIndex, std::move(definition));
}

void QcContentRegistry::registerStructure(QcStructureDefinition structure)
{
    registerByName(m_structures, m_structureIndex, std::move(structure));
}

const QcStructureDefinition *QcContentRegistry::structure(const QString &name) const
{
    const auto it = m_structureIndex.constFind(name);
    return it != m_structureIndex.constEnd() ? &m_structures[*it] : nullptr;
}

void QcContentRegistry::registerCommandMacro(QcCommandMacroDefinition macro)
{
    registerByName(m_commandMacros, m_commandMacroIndex, std::move(macro));
}

const QcCommandMacroDefinition *QcContentRegistry::commandMacro(const QString &name) const
{
    const auto it = m_commandMacroIndex.constFind(name);
    return it != m_commandMacroIndex.constEnd() ? &m_commandMacros[*it] : nullptr;
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
    QcContentSerializer::write(*this, stream);
}

QcResult<QcContentRegistry> QcContentRegistry::readFrom(QDataStream &stream)
{
    return QcContentSerializer::read(stream);
}

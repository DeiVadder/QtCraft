#include "worldgen/showcase/QcShowcasePlanner.h"

#include "worldgen/showcase/QcShowcaseArmourArea.h"
#include "worldgen/showcase/QcShowcaseBlockArea.h"
#include "worldgen/showcase/QcShowcaseCategories.h"
#include "worldgen/showcase/QcShowcaseCreatureArea.h"
#include "worldgen/showcase/QcShowcaseItemArea.h"
#include "worldgen/showcase/QcShowcaseTreeArea.h"
#include "worldgen/showcase/QcShowcaseVillageArea.h"

#include <QDebug>
#include <QtMath>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// Ground used when the content brings no showcase settings: like a plain field.
constexpr int defaultStoneDepth = 59;
// Titles float this high above the ground at the south west corner of their area.
constexpr double titleHeight = 6.5;
// Eye height of a standing player (QcPhysics::playerEyeHeight, kept free of the physics module).
constexpr double eyeHeight = 1.62;

QcBlockId resolved(const QcContentRegistry &content, const QString &name, const QString &fallback)
{
    return content.blockId(name).value_or(content.blockId(fallback).value_or(qcAirBlockId));
}

QList<QcBlockId> groundColumnOf(const QcContentRegistry &content)
{
    QList<QcShowcaseDefinition::Layer> layers = content.showcase().layers;
    if (layers.isEmpty()) {
        layers = {{u"qtcraft:bedrock"_s, 1}, {u"qtcraft:stone"_s, defaultStoneDepth}, {u"qtcraft:dirt"_s, 3}, {u"qtcraft:grass_block"_s, 1}};
    }

    QList<QcBlockId> column;
    for (const QcShowcaseDefinition::Layer &layer : std::as_const(layers)) {
        const QcBlockId block = resolved(content, layer.block, u"qtcraft:stone"_s);
        column.insert(column.size(), layer.depth, block);
    }
    return column;
}

// View from `eye` (column centre at that height) to `target`.
QcShowcaseTourStop stopLookingAt(const QString &name, QcBlockPos column, double eyeY, const QcVec3 &target)
{
    const double dx = target.x - (column.x + 0.5);
    const double dz = target.z - (column.z + 0.5);
    const double horizontal = std::max(1.0, std::hypot(dx, dz));
    const float yaw = float(qRadiansToDegrees(std::atan2(dx, -dz)));
    const float pitch = float(-qRadiansToDegrees(std::atan2(eyeY - target.y, horizontal)));
    return {name, column.x, column.z, yaw < 0.0f ? yaw + 360.0f : yaw, pitch};
}

} // namespace

qsizetype QcShowcasePlan::labelCount() const
{
    qsizetype count = 0;
    for (const QList<QcWorldLabel> &labels : labelsByChunk) {
        count += labels.size();
    }
    return count;
}

QcShowcasePlanner::QcShowcasePlanner(const QcContentRegistry &content, QStringList *warnings)
    : m_content(content)
    , m_warnings(warnings)
    , m_groundColumn(groundColumnOf(content))
{
    const QString lookout = content.showcase().lookout;
    if (const QcStructureDefinition *definition = lookout.isEmpty() ? nullptr : content.structure(lookout)) {
        QcResult<QcStructure> structure = QcStructure::fromDefinition(*definition, content, warnings);
        if (structure) {
            m_lookout = std::make_shared<const QcStructure>(std::move(*structure));
        } else if (warnings) {
            warnings->append(structure.error());
        }
    } else if (!lookout.isEmpty() && warnings) {
        warnings->append(u"showcase: unknown lookout structure '%1'"_s.arg(lookout));
    }
}

QcShowcaseContext QcShowcasePlanner::context() const
{
    const QcShowcaseDefinition &settings = m_content.showcase();
    return {m_content,
            settings,
            int(m_groundColumn.size()) - 1,
            m_groundColumn.constLast(),
            resolved(m_content, settings.podium, u"qtcraft:stone"_s),
            resolved(m_content, settings.enclosure, u"qtcraft:glass"_s),
            resolved(m_content, settings.container, u"qtcraft:chest"_s)};
}

QList<QcShowcaseAreaBuilder> QcShowcasePlanner::demoBuilders() const
{
    QList<const QcStructureDefinition *> demos;
    for (const QcStructureDefinition &structure : m_content.structures()) {
        if (structure.showcase) {
            demos.append(&structure);
        }
    }
    std::ranges::sort(demos, [](const QcStructureDefinition *a, const QcStructureDefinition *b) {
        return std::tie(a->showcaseOrder, a->name) < std::tie(b->showcaseOrder, b->name);
    });

    QList<QcShowcaseAreaBuilder> builders;
    for (const QcStructureDefinition *definition : std::as_const(demos)) {
        QcResult<QcStructure> structure = QcStructure::fromDefinition(*definition, m_content, m_warnings);
        if (!structure) {
            if (m_warnings) {
                m_warnings->append(structure.error());
            }
            continue;
        }

        auto shared = std::make_shared<const QcStructure>(std::move(*structure));
        const QString name = u"demo-"_s + definition->name.mid(definition->name.indexOf(u':') + 1);
        const QString title = definition->translationKey;
        builders.append([shared, name, title](QcBlockPos anchor) {
            return QcShowcaseArea{name, title, shared, {anchor.x, anchor.y, anchor.z - shared->sizeZ() + 1}, {}, {}};
        });
    }
    return builders;
}

QList<QcShowcaseAreaBuilder> QcShowcasePlanner::builders(const QcShowcaseContext &context) const
{
    QList<QcShowcaseAreaBuilder> builders = demoBuilders();
    builders.append([context](QcBlockPos anchor) { return QcShowcaseTreeArea::build(context, anchor); });
    builders.append([context](QcBlockPos anchor) { return QcShowcaseCreatureArea::build(context, anchor); });
    if (!QcShowcaseArmourArea::sets(m_content).isEmpty()) {
        builders.append([context](QcBlockPos anchor) { return QcShowcaseArmourArea::build(context, anchor); });
    }

    QList<QcShowcaseCategory> categories = m_content.showcase().categories;
    if (categories.isEmpty()) {
        categories.append({u"blocks"_s, u"showcase.category.blocks"_s, {}});
    }
    const QList<QList<quint32>> sorted = QcShowcaseCategories::sort(m_content);
    for (qsizetype i = 0; i < categories.size() && i < sorted.size(); ++i) {
        if (sorted[i].isEmpty()) {
            continue;
        }
        // The builder keeps its own copies: it runs again when its area moves to a new row.
        builders.append([context, category = categories[i], types = sorted[i]](QcBlockPos anchor) {
            return QcShowcaseBlockArea::build(context, category, types, anchor);
        });
    }

    builders.append([context](QcBlockPos anchor) { return QcShowcaseItemArea::build(context, anchor); });
    // Last, so the village rows (wide and tall) move no other area.
    if (QcShowcaseVillageArea::hasStyles(m_content)) {
        builders.append([context](QcBlockPos anchor) { return QcShowcaseVillageArea::build(context, anchor); });
    }
    return builders;
}

QcShowcaseArea QcShowcasePlanner::placeNext(const QcShowcaseAreaBuilder &builder, Cursor &cursor) const
{
    const int rowEnd = firstAreaX + m_content.showcase().rowWidth;
    QcShowcaseArea area = builder({cursor.x, int(m_groundColumn.size()) - 1, cursor.front});
    if (cursor.x > firstAreaX && cursor.x + area.structure->sizeX() > rowEnd) {
        cursor = {firstAreaX, cursor.north - rowGap, cursor.north - rowGap};
        area = builder({cursor.x, int(m_groundColumn.size()) - 1, cursor.front});
    }

    cursor.x += area.structure->sizeX() + areaGap;
    cursor.north = std::min(cursor.north, area.origin.z);
    return area;
}

QcShowcasePlan QcShowcasePlanner::build()
{
    m_plan = {};
    m_plan.groundColumn = m_groundColumn;
    const QcShowcaseContext areaContext = context();
    m_plan.spawn = {0, areaContext.groundTop + 1, 0};
    m_plan.setup.timeOfDay = m_content.showcase().timeOfDay;
    m_plan.setup.daylightCycle = false;
    // Every crop stage and soil state on show stays as it is (decision E-295).
    m_plan.setup.randomTicks = false;
    // Fire on show burns where it is and never reaches the stations (decision E-392).
    m_plan.setup.fireSpread = false;
    // The sky stays clear for the screenshots of the tour (decision E-395).
    m_plan.setup.weatherCycle = false;
    m_plan.setup.mobSpawning = false;
    // The settlement's farmers harvest and pick up bread; the pens' creatures are confined and leave
    // the demos as they are (decisions E-460, E-510).
    m_plan.setup.mobGriefing = true;

    Cursor cursor;
    const QList<QcShowcaseAreaBuilder> areaBuilders = builders(areaContext);
    for (const QcShowcaseAreaBuilder &builder : areaBuilders) {
        // placeNext() moves the cursor along the row; its front is the one the area was built at.
        const QcShowcaseArea area = placeNext(builder, cursor);
        addArea(area, cursor.front);
    }
    return std::move(m_plan);
}

void QcShowcasePlanner::addArea(const QcShowcaseArea &area, int front)
{
    addPlacement(QcStructurePlacement(area.structure, area.origin, 0, m_content));
    const QcStructure &structure = *area.structure;
    m_plan.areas.append({area.name, area.title, area.origin, structure.sizeX(), structure.sizeY(), structure.sizeZ()});
    m_plan.villages.append(area.villages);
    for (const QcBlockPos &point : area.pointsOfInterest) {
        m_plan.pointsOfInterestByChunk[point.chunk()].append(point);
    }
    addLabel({{area.origin.x + 1.0, area.origin.y + titleHeight, front + 1.5}, area.title, {}, true});
    addLookout(area, front);

    // The near stop looks north along the west part of the area from the walkway in front of it.
    const QcBlockPos near{area.origin.x + 3, 0, front + 6};
    const QcVec3 nearTarget{near.x + 0.5, area.origin.y + 1.0, std::max(double(area.origin.z), front - 4.0)};
    m_plan.tour.append(stopLookingAt(area.name + u"-near"_s, near, area.origin.y + 1 + eyeHeight, nearTarget));
}

void QcShowcasePlanner::addLookout(const QcShowcaseArea &area, int front)
{
    const QcStructure &structure = *area.structure;
    const QcVec3 centre{area.origin.x + structure.sizeX() / 2.0, double(area.origin.y + 1), area.origin.z + structure.sizeZ() / 2.0};
    if (!m_lookout) {
        const QcBlockPos column{area.origin.x - 3, 0, front + 5};
        m_plan.tour.append(stopLookingAt(area.name, column, area.origin.y + 1 + eyeHeight, centre));
        return;
    }

    // The lookout stands in front of the area's south west corner; the stop is the north east corner
    // of its top, so the platform stays out of the view.
    const QcBlockPos origin{area.origin.x - m_lookout->sizeX() - 1, area.origin.y, front + 2};
    addPlacement(QcStructurePlacement(m_lookout, origin, 0, m_content));
    const QcBlockPos column{origin.x + m_lookout->sizeX() - 1, 0, origin.z};
    m_plan.tour.append(stopLookingAt(area.name, column, area.origin.y + m_lookout->sizeY() + eyeHeight, centre));
}

void QcShowcasePlanner::addPlacement(QcStructurePlacement placement)
{
    const qsizetype index = m_plan.placements.size();
    const QcStructure &structure = placement.structure();
    for (const QcStructure::Label &label : structure.labels) {
        addLabel({placement.toWorld(label.at), label.text, label.detail, label.heading});
    }
    for (const QcStructure::Mob &mob : structure.mobs) {
        m_plan.setup.mobs.append({mob.mob, placement.toWorld(mob.at), mob.confined, mob.owner, mob.sitting, mob.noAi});
    }
    for (const QcStructure::Stand &stand : structure.stands) {
        m_plan.setup.stands.append({placement.toWorld(stand.at), stand.yaw + 90.0f * float(placement.turns()), stand.properties});
    }
    for (const QcStructure::Contents &contents : structure.contents) {
        m_plan.setup.contents.append({placement.toWorld(contents.at), contents.items});
    }
    for (const QcBlockPos &pos : structure.updates) {
        m_plan.setup.updates.append(placement.toWorld(pos));
    }
    for (const QcStructureDefinition::Cart &cart : structure.minecarts) {
        m_plan.setup.minecarts.append({placement.toWorld(cart.at), cart.item});
    }
    for (const QcBlockPos &pos : structure.growing) {
        m_plan.setup.growing.append(placement.toWorld(pos));
    }
    for (const QcStructureDefinition::Spawner &spawner : structure.spawners) {
        const QcBlockPos at = placement.toWorld(spawner.at);
        m_plan.furnishingByChunk[at.chunk()].spawners.append({at, spawner.mob, spawner.confined});
    }
    for (const QcStructureDefinition::Loot &loot : structure.loot) {
        const QcBlockPos at = placement.toWorld(loot.at);
        m_plan.furnishingByChunk[at.chunk()].chests.append({at, loot.table, quint64(qHash(at))});
    }
    m_plan.guardSites.append(QcGuardSite::of(placement));

    const QcChunkPos first = placement.origin().chunk();
    const QcChunkPos last = placement.origin().offset(placement.sizeX() - 1, 0, placement.sizeZ() - 1).chunk();
    for (int cx = first.x; cx <= last.x; ++cx) {
        for (int cz = first.z; cz <= last.z; ++cz) {
            m_plan.placementsByChunk[{cx, cz}].append(index);
        }
    }
    m_plan.placements.append(std::move(placement));
}

void QcShowcasePlanner::addLabel(const QcWorldLabel &label)
{
    const QcBlockPos pos{int(std::floor(label.position.x)), 0, int(std::floor(label.position.z))};
    QList<QcWorldLabel> &labels = m_plan.labelsByChunk[pos.chunk()];
    if (labels.size() >= QcWorldLabel::maxPerChunk) {
        warn(u"showcase: more than %1 labels in chunk %2, %3; '%4' is left out"_s.arg(QcWorldLabel::maxPerChunk).arg(pos.chunk().x).arg(pos.chunk().z).arg(label.text));
        return;
    }

    // Long state descriptions of mod blocks are cut here, before they would be refused on the wire.
    labels.append(label.limited());
}

void QcShowcasePlanner::warn(const QString &warning) const
{
    if (m_warnings) {
        m_warnings->append(warning);
    } else {
        qWarning().noquote() << warning;
    }
}

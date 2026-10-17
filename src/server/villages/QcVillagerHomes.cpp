#include "server/villages/QcVillagerHomes.h"

#include "common/content/registry/QcContentRegistry.h"
#include "server/blocks/QcBlockRules.h"
#include "server/mobs/QcMobSteering.h"
#include "server/villages/QcVillages.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <array>
#include <limits>

using namespace Qt::StringLiterals;

namespace {

// How far towards the head the pivot of a lying body sits from the foot block's centre: its feet
// (QcPhysics::lyingPivot beyond it) a tenth of a block inside the bed's foot end, so a body of 1.9 m
// ends on the pillow and not in the wall behind it.
constexpr double lyingShift = -0.25;

double distanceTo(QcBlockPos spot, const QcVec3 &near)
{
    return std::hypot(spot.x + 0.5 - near.x, spot.z + 0.5 - near.z) + std::abs(spot.y - near.y);
}

// The free spot nearest to `near` that `fits`; null when there is none.
template<typename Fits>
QcVillage::Spot *nearestFree(QList<QcVillage::Spot> &spots, const QcVec3 &near, Fits fits)
{
    QcVillage::Spot *best = nullptr;
    double bestDistance = std::numeric_limits<double>::max();
    for (QcVillage::Spot &spot : spots) {
        const double distance = distanceTo(spot.at, near);
        if (spot.claimedBy.isEmpty() && fits(spot) && distance < bestDistance) {
            best = &spot;
            bestDistance = distance;
        }
    }
    return best;
}

// Whether `spot` at `at` is still listed and held by `name`.
bool holds(QList<QcVillage::Spot> &spots, std::optional<QcBlockPos> at, const QString &name)
{
    const QcVillage::Spot *spot = at ? QcVillage::spotAt(spots, *at) : nullptr;
    return spot && spot->claimedBy == name;
}

void unclaim(QList<QcVillage::Spot> &spots, std::optional<QcBlockPos> at, const QString &name)
{
    if (QcVillage::Spot *spot = at ? QcVillage::spotAt(spots, *at) : nullptr; spot && spot->claimedBy == name) {
        spot->claimedBy.clear();
    }
}

} // namespace

namespace QcVillagerHomes {

QcVillager settle(QcVillage &village, QcBlockPos bed, const QcVillagerSettings &settings, quint32 look)
{
    QcVillager villager;
    villager.village = village.id;
    villager.name = freshName(village);
    villager.style = village.style;
    villager.look = look;
    villager.profession = settings.unemployed;
    if (QcVillage::Spot *spot = QcVillage::spotAt(village.beds, bed); spot && spot->claimedBy.isEmpty()) {
        spot->claimedBy = villager.name;
        villager.home = bed;
    }
    ++village.population;
    return villager;
}

QList<QcBlockPos> freeBedsIn(const QcVillage &village, QcChunkPos chunk)
{
    QList<QcBlockPos> beds;
    for (const QcVillage::Spot &bed : village.beds) {
        if (bed.claimedBy.isEmpty() && bed.at.chunk() == chunk) {
            beds.append(bed.at);
        }
    }
    return beds;
}

std::optional<QcVec3> besideBed(const QcBlockAccess &world, const QcPathCellTable &cells, QcBlockPos head, QcPathRules rules)
{
    const std::optional<QcBlockId> block = world.blockAt(head);
    if (!block || !world.content().block(*block).bed) {
        return std::nullopt;
    }

    // The cells around both halves at the bed's height and one below or above, the foot's first.
    const QcBlockPos foot = QcBlockRules::otherHalf(world, head, *block);
    const QcPathDirectCells direct(world, cells);
    constexpr std::array<std::pair<int, int>, 8> around{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, -1}, {1, -1}, {-1, 1}}};
    // A place to stand around `level` (a half of the bed, raised or lowered) that is not part of a bed.
    const auto standAround = [&](QcBlockPos level) -> std::optional<QcVec3> {
        for (const auto &[dx, dz] : around) {
            const QcBlockPos cell = level.offset(dx, 0, dz);
            const std::optional<QcBlockId> there = world.blockAt(cell);
            const QcPathStand stand = QcPathTerrain::standAt(direct, cell, rules);
            if (there && !world.content().block(*there).bed && stand.ok) {
                return QcVec3{cell.x + 0.5, cell.y + stand.lift(), cell.z + 0.5};
            }
        }
        return std::nullopt;
    };

    for (const QcBlockPos half : {foot, head}) {
        for (const int dy : {0, -1, 1}) {
            if (const std::optional<QcVec3> at = standAround(half.offset(0, dy, 0))) {
                return at;
            }
        }
    }
    return std::nullopt;
}

std::optional<Lying> lyingIn(const QcBlockAccess &world, QcBlockPos head)
{
    const std::optional<QcBlockId> block = world.blockAt(head);
    const QcContentRegistry &content = world.content();
    if (!block || !content.block(*block).bed) {
        return std::nullopt;
    }

    const QcBlockPos foot = QcBlockRules::otherHalf(world, head, *block);
    double top = 0.0;
    for (const QcAabb &box : content.state(*block).collision) {
        top = std::max(top, box.max.y);
    }
    const QcVec3 towardsHead{double(head.x - foot.x), 0.0, double(head.z - foot.z)};
    const QcVec3 centre{foot.x + 0.5, foot.y + top, foot.z + 0.5};
    return Lying{centre + towardsHead * lyingShift, QcMobSteering::yawTowards(towardsHead * -1.0)};
}

void keepClaims(QcVillage &village, QcVillager &villager, const QcVillagerSettings &settings)
{
    if (villager.home && !holds(village.beds, villager.home, villager.name)) {
        villager.home.reset();
    }
    if (villager.work && !holds(village.workstations, villager.work, villager.name)) {
        villager.work.reset();
        if (villager.experience == 0 && villager.profession != settings.unemployed) {
            villager.profession = settings.unemployed;
            villager.lookChanged = true;
        }
    }
}

bool claimBed(QcVillage &village, QcVillager &villager, const QcVec3 &near)
{
    QcVillage::Spot *bed = nearestFree(village.beds, near, [](const QcVillage::Spot &) { return true; });
    if (!bed) {
        return false;
    }
    bed->claimedBy = villager.name;
    villager.home = bed->at;
    return true;
}

bool claimWorkstation(QcVillage &village, QcVillager &villager, const QcVillagerSettings &settings, const QcVec3 &near)
{
    const QcVillagerProfession *own = settings.profession(villager.profession);
    const bool unemployed = !own || own->workstation.isEmpty();
    QcVillage::Spot *station = nearestFree(village.workstations, near, [&](const QcVillage::Spot &spot) {
        return unemployed ? settings.forWorkstation(spot.kind) != nullptr : spot.kind == own->workstation;
    });
    if (!station) {
        return false;
    }

    station->claimedBy = villager.name;
    villager.work = station->at;
    if (unemployed) {
        villager.profession = settings.forWorkstation(station->kind)->name;
        villager.lookChanged = true;
    }
    return true;
}

void release(QcVillages &villages, const QcVillager &villager)
{
    QcVillage *village = villager.village != 0 ? villages.find(villager.village) : nullptr;
    if (!village) {
        return;
    }
    unclaim(village->beds, villager.home, villager.name);
    unclaim(village->workstations, villager.work, villager.name);
    village->population = std::max(0, village->population - 1);
    villages.noteChanged();
}

QString freshName(QcVillage &village)
{
    const auto claimed = [&village](const QString &name) {
        const auto held = [&name](const QcVillage::Spot &spot) { return spot.claimedBy == name; };
        return std::ranges::any_of(village.beds, held) || std::ranges::any_of(village.workstations, held);
    };
    QString name;
    do {
        name = u"villager-%1"_s.arg(village.nextVillager++);
    } while (claimed(name));
    return name;
}

int numberOf(const QString &name)
{
    constexpr QStringView prefix = u"villager-";
    bool ok = false;
    const int number = name.startsWith(prefix) ? QStringView(name).sliced(prefix.size()).toInt(&ok) : 0;
    return ok && number > 0 ? number : 0;
}

bool noteName(QcVillage &village, const QString &name)
{
    const int number = numberOf(name);
    if (number < village.nextVillager || number >= std::numeric_limits<int>::max() - 1) {
        return false;
    }
    village.nextVillager = number + 1;
    return true;
}

int dropLostClaims(QcVillage &village, const QSet<QString> &present)
{
    QSet<QString> lost;
    for (QList<QcVillage::Spot> *spots : {&village.beds, &village.workstations}) {
        for (QcVillage::Spot &spot : *spots) {
            if (!spot.claimedBy.isEmpty() && !present.contains(spot.claimedBy)) {
                lost.insert(spot.claimedBy);
                spot.claimedBy.clear();
            }
        }
    }
    return int(lost.size());
}

} // namespace QcVillagerHomes

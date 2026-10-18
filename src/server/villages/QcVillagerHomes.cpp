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
        if (spot.claimedBy == 0 && fits(spot) && distance < bestDistance) {
            best = &spot;
            bestDistance = distance;
        }
    }
    return best;
}

// Whether `spot` at `at` is still listed and held by `holder`.
bool holds(QList<QcVillage::Spot> &spots, std::optional<QcBlockPos> at, quint64 holder)
{
    const QcVillage::Spot *spot = at ? QcVillage::spotAt(spots, *at) : nullptr;
    return spot && spot->claimedBy == holder;
}

void unclaim(QList<QcVillage::Spot> &spots, std::optional<QcBlockPos> at, quint64 holder)
{
    if (QcVillage::Spot *spot = at ? QcVillage::spotAt(spots, *at) : nullptr; spot && spot->claimedBy == holder) {
        spot->claimedBy = 0;
    }
}

} // namespace

namespace QcVillagerHomes {

QcVillager settle(QcVillage &village, QcBlockPos bed, quint64 holder, const QcVillagerSettings &settings, quint32 look)
{
    QcVillager villager;
    villager.village = village.id;
    villager.style = village.style;
    villager.look = look;
    villager.profession = settings.unemployed;
    if (QcVillage::Spot *spot = QcVillage::spotAt(village.beds, bed); spot && spot->claimedBy == 0) {
        spot->claimedBy = holder;
        villager.home = bed;
    }
    ++village.population;
    village.residents.uids.insert(holder);
    return villager;
}

QList<QcBlockPos> freeBedsIn(const QcVillage &village, QcChunkPos chunk)
{
    QList<QcBlockPos> beds;
    for (const QcVillage::Spot &bed : village.beds) {
        if (bed.claimedBy == 0 && bed.at.chunk() == chunk) {
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

void keepClaims(QcVillage &village, QcVillager &villager, quint64 holder, const QcVillagerSettings &settings)
{
    if (villager.home && !holds(village.beds, villager.home, holder)) {
        villager.home.reset();
    }
    if (villager.work && !holds(village.workstations, villager.work, holder)) {
        villager.work.reset();
        if (villager.experience == 0 && villager.profession != settings.unemployed) {
            villager.profession = settings.unemployed;
            villager.lookChanged = true;
        }
    }
}

bool claimBed(QcVillage &village, QcVillager &villager, quint64 holder, const QcVec3 &near)
{
    QcVillage::Spot *bed = nearestFree(village.beds, near, [](const QcVillage::Spot &) { return true; });
    if (!bed) {
        return false;
    }
    bed->claimedBy = holder;
    villager.home = bed->at;
    return true;
}

bool claimWorkstation(QcVillage &village, QcVillager &villager, quint64 holder, const QcVillagerSettings &settings, const QcVec3 &near)
{
    const QcVillagerProfession *own = settings.profession(villager.profession);
    const bool unemployed = !own || own->workstation.isEmpty();
    QcVillage::Spot *station = nearestFree(village.workstations, near, [&](const QcVillage::Spot &spot) {
        return unemployed ? settings.forWorkstation(spot.kind) != nullptr : spot.kind == own->workstation;
    });
    if (!station) {
        return false;
    }

    station->claimedBy = holder;
    villager.work = station->at;
    if (unemployed) {
        villager.profession = settings.forWorkstation(station->kind)->name;
        villager.lookChanged = true;
    }
    return true;
}

void release(QcVillages &villages, const QcVillager &villager, quint64 holder)
{
    QcVillage *village = villager.village != 0 ? villages.find(villager.village) : nullptr;
    if (!village) {
        return;
    }
    unclaim(village->beds, villager.home, holder);
    unclaim(village->workstations, villager.work, holder);
    // One the reconciled village no longer counts (away when it was reconciled) left its population
    // then already.
    const bool counted = !village->residents.known || village->residents.uids.remove(holder);
    village->population = std::max(0, village->population - (counted ? 1 : 0));
    villages.noteChanged();
    villages.dropIfEmpty(village->id);
}

void moveOut(QcVillages &villages, QcVillager &villager, quint64 holder, const QcVillagerSettings &settings)
{
    release(villages, villager, holder);
    villager.village = 0;
    villager.home.reset();
    villager.work.reset();
    villager.target.reset();
    villager.crop.reset();
    if (villager.experience == 0 && villager.profession != settings.unemployed) {
        villager.profession = settings.unemployed;
        villager.lookChanged = true;
    }
}

void moveIn(QcVillage &village, QcVillager &villager, quint64 holder, const QcVec3 &near)
{
    villager.village = village.id;
    if (village.style.isEmpty()) {
        village.style = villager.style;
    }
    ++village.population;
    village.residents.uids.insert(holder);
    claimBed(village, villager, holder, near);
}

int dropLostClaims(QcVillage &village, const QSet<quint64> &present)
{
    QSet<quint64> lost;
    for (QList<QcVillage::Spot> *spots : {&village.beds, &village.workstations}) {
        for (QcVillage::Spot &spot : *spots) {
            if (spot.claimedBy != 0 && !present.contains(spot.claimedBy)) {
                lost.insert(spot.claimedBy);
                spot.claimedBy = 0;
            }
        }
    }
    return int(lost.size());
}

} // namespace QcVillagerHomes

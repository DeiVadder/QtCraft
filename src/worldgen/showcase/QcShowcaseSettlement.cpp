#include "worldgen/showcase/QcShowcaseSettlement.h"

#include "worldgen/villages/QcVillagePlan.h"

using namespace Qt::StringLiterals;

namespace {

int floorMod(int value, int modulus)
{
    return ((value % modulus) + modulus) % modulus;
}

// Places `block` (a state name) at the local cell when the content has it; whether it did.
bool place(const QcShowcaseContext &context, QcStructure &area, QcBlockPos cell, const QString &block)
{
    const std::optional<QcBlockId> state = context.content.stateId(block);
    if (state) {
        area.set(cell.x, cell.y, cell.z, *state);
    }
    return state.has_value();
}

// A bed lying north with its head at the local cell `head`; whether the content has beds.
bool bed(const QcShowcaseContext &context, QcStructure &area, QcBlockPos head)
{
    return place(context, area, head, u"qtcraft:bed[facing=north,part=head]"_s)
           && place(context, area, head.offset(0, 0, 1), u"qtcraft:bed[facing=north,part=foot]"_s);
}

// The ripe wheat field south of the beds, within QcVillagerBehaviour::cropRadius (8) of the
// composter at local (x + 11, z + 3), watered from its middle.
void field(const QcShowcaseContext &context, QcStructure &area, int x, int z)
{
    for (int dx = 4; dx <= 12; ++dx) {
        for (int dz = 7; dz <= 9; ++dz) {
            if (dx == 8 && dz == 8) {
                place(context, area, {x + dx, 0, z + dz}, u"qtcraft:water"_s);
            } else if (place(context, area, {x + dx, 0, z + dz}, u"qtcraft:farmland"_s)) {
                place(context, area, {x + dx, 1, z + dz}, u"qtcraft:wheat_crop[age=7]"_s);
            }
        }
    }
}

// The rail with a minecart south of the field: push a villager in and carry it away.
void rail(const QcShowcaseContext &context, QcStructure &area, int x, int z)
{
    bool railed = true;
    for (int dx = 2; dx <= 13; ++dx) {
        railed = place(context, area, {x + dx, 1, z + 12}, u"qtcraft:rail[facing=east,shape=straight]"_s) && railed;
    }
    if (railed && context.content.itemId(u"qtcraft:minecart"_s)) {
        area.addCart({{x + 3, 1, z + 12}, u"qtcraft:minecart"_s});
    }
}

} // namespace

namespace QcShowcaseSettlement {

int settlementX(int rowsWidth, int west)
{
    const int x = rowsWidth + gap;
    return x + floorMod(-(west + x), QcWorld::chunkSize);
}

int areaWidth(int x)
{
    return x + QcWorld::chunkSize + QcVillagePlan::reach + 1;
}

std::optional<int> settlementZ(int depth, int north, int frontGap)
{
    int z = depth - frontGap - QcWorld::chunkSize;
    z -= floorMod(north + z, QcWorld::chunkSize);
    return z >= 0 ? std::optional(z) : std::nullopt;
}

QList<QcBlockPos> build(const QcShowcaseContext &context, QcStructure &area, QcBlockPos origin, Frame frame)
{
    QList<QcBlockPos> points;
    const auto world = [origin](QcBlockPos cell) { return origin.offset(cell.x, cell.y, cell.z); };

    // The bed beyond the first row: its village grows to hold it.
    const QcBlockPos growth{frame.rowsWidth - 1 + growthGap, 1, frame.firstRoad - 3};
    if (bed(context, area, growth)) {
        points.append(world(growth));
        area.addLabel({{growth.x + 0.5, 3.5, growth.z + 0.5}, u"showcase.station.growth"_s, {}, false});
    }

    // The settlement: three beds and a composter in one chunk, so its id (its first spot) is the
    // same in any loading order.
    const int x = frame.x;
    const int z = frame.z;
    for (int i = 0; i < 3; ++i) {
        const QcBlockPos head{x + 2 + 3 * i, 1, z + 3};
        if (bed(context, area, head)) {
            points.append(world(head));
        }
    }
    const QcBlockPos composter{x + 11, 1, z + 3};
    if (place(context, area, composter, u"qtcraft:composter"_s)) {
        points.append(world(composter));
    }
    const QcBlockPos chest{x + 13, 1, z + 3};
    if (place(context, area, chest, u"qtcraft:chest[facing=south]"_s) && context.content.itemId(u"qtcraft:bread"_s)) {
        area.addContents({chest, {{u"qtcraft:bread"_s, 16}}});
    }
    field(context, area, x, z);
    rail(context, area, x, z);
    if (context.content.mobIndex(u"qtcraft:villager"_s)) {
        area.addMob({u"qtcraft:villager"_s, {x + 5.5, 1.0, z + 6.0}, false});
        area.addMob({u"qtcraft:villager"_s, {x + 9.5, 1.0, z + 6.0}, false});
    }
    area.addLabel({{x + 8.0, 3.5, z + 1.5}, u"showcase.station.settlement"_s, {}, false});
    return points;
}

} // namespace QcShowcaseSettlement

// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/bastion/QcBastionForms.h"

#include <QString>

#include <utility>

using namespace Qt::StringLiterals;

namespace {

using Sketch = QcBastionSketch;
using Side = QcBastionPlan::Side;
using Paint = QcBastionPlan::Paint;
using Loot = QcBastionPlan::Loot;
using Folk = QcBastionPlan::Folk;

// Floors of a storey in a tower of the bridge and in a house.
constexpr int towerStorey = 9;
constexpr int houseStorey = 6;
// The height of the causeway's deck: the first floor of its towers.
constexpr int deckHeight = towerStorey;
// A wide gate.
constexpr QcBastionDoorway gate{2, 4, 2};

// Ragged tops: the top four cells of `box` and a row over them fall away.
void weather(Sketch &sketch, const QcBlockBox &box)
{
    sketch.merlons(box);
    sketch.ruin({{box.low.x, box.high.y - 3, box.low.z}, box.high.offset(0, 1, 0)});
}

// A hall with floors every `storey` cells, its top weathered.
void tower(Sketch &sketch, const QcBlockBox &box, int storey)
{
    sketch.hall(box);
    for (int y = box.low.y + storey; y < box.high.y - 1; y += storey) {
        sketch.paint({{box.low.x + 1, y, box.low.z + 1}, {box.high.x - 1, y, box.high.z - 1}}, Paint::Brick);
    }
    weather(sketch, box);
}

// A lantern on a short post of wall.
void lanternPost(Sketch &sketch, QcBlockPos at)
{
    sketch.paint({at, at}, Paint::Wall);
    sketch.paint({at.offset(0, 1, 0), at.offset(0, 1, 0)}, Paint::Lantern);
}

void canal(Sketch &sketch)
{
    sketch.paint({{-34, -1, -9}, {34, -1, 9}}, Paint::Foundation);
    sketch.paint({{-34, 0, -9}, {34, 2, 9}}, Paint::Brick);
    sketch.paint({{-34, 1, -8}, {34, 2, 8}}, Paint::Lava);
    sketch.paint({{-34, 3, -9}, {34, deckHeight + 6, 9}}, Paint::Air);
    sketch.addPart({{-34, 0, -9}, {34, 2, 9}});
}

void causeway(Sketch &sketch)
{
    for (const int x : {-24, -12, 12, 24}) {
        sketch.paint({{x - 1, 1, -3}, {x + 1, deckHeight - 1, 3}}, Paint::Brick);
        sketch.paint({{x - 3, deckHeight - 1, -3}, {x + 3, deckHeight - 1, 3}}, Paint::Polished);
    }
    sketch.deck({{-34, deckHeight, -3}, {34, deckHeight + 5, 3}}, Side::East);
    sketch.paint({{-34, deckHeight, -1}, {34, deckHeight, 1}}, Paint::Polished);
    sketch.ruin({{-34, deckHeight + 1, -3}, {-8, deckHeight + 1, 3}});
    sketch.ruin({{8, deckHeight + 1, -3}, {34, deckHeight + 1, 3}});
    for (const int x : {-30, -18, 18, 30}) {
        lanternPost(sketch, {x, deckHeight + 1, -3});
        lanternPost(sketch, {x, deckHeight + 1, 3});
    }

    // The gatehouse in the middle of the span.
    sketch.hall({{-6, deckHeight, -7}, {6, deckHeight + 9, 7}});
    sketch.door({-6, deckHeight + 1, 0}, Side::West, {2, 4, 1});
    sketch.door({6, deckHeight + 1, 0}, Side::East, {2, 4, 1});
    weather(sketch, {{-6, deckHeight, -7}, {6, deckHeight + 9, 7}});
    sketch.chest({0, deckHeight + 1, -5}, Side::South, Loot::Other);
}

void bridgeTowers(Sketch &sketch)
{
    tower(sketch, {{-58, 0, -14}, {-35, 19, 14}}, towerStorey);
    sketch.flight({-55, 0, -12}, Side::East, towerStorey, 3);
    sketch.door({-58, 1, 0}, Side::West, {2, 4, 1});
    sketch.landing({-58, 0, 0}, Side::West);
    sketch.door({-35, deckHeight + 1, 0}, Side::East);

    tower(sketch, {{35, 0, -16}, {58, 28, 16}}, towerStorey);
    sketch.door({35, deckHeight + 1, 0}, Side::West);
    sketch.flight({38, 0, -14}, Side::East, towerStorey, 3);
    sketch.flight({38, deckHeight, 12}, Side::East, towerStorey, 3);

    // The strong room on the top floor.
    constexpr int top = 2 * towerStorey;
    sketch.paint({{50, top, -3}, {55, top, 3}}, Paint::Gilded);
    sketch.paint({{51, top + 1, -2}, {54, top + 1, 2}}, Paint::Chiseled);
    sketch.chest({53, top + 2, 0}, Side::West, Loot::Bridge);
}

void bridgeFolk(Sketch &sketch)
{
    constexpr int onDeck = deckHeight + 1;
    for (const QcBlockPos &guard : {QcBlockPos{-20, onDeck, 0}, {20, onDeck, 0}, {0, onDeck, 0}, {-44, 1, 6}, {-44, onDeck, 6}, {46, 1, 6}}) {
        sketch.resident(guard, Folk::Guard);
    }
    sketch.resident({46, onDeck, 6}, Folk::Bruiser);
    sketch.resident({46, 2 * towerStorey + 1, 0}, Folk::Bruiser);
}

void bridge(Sketch &sketch)
{
    canal(sketch);
    causeway(sketch);
    bridgeTowers(sketch);
    bridgeFolk(sketch);
    sketch.sign({-61, 4, 0}, u"structure.qtcraft.bastion_bridge"_s, true);
    sketch.sign({-28, deckHeight + 3, 0}, u"structure.qtcraft.bastion_causeway"_s);
    sketch.sign({53, 2 * towerStorey + 3, 0}, u"structure.qtcraft.bastion_chest"_s);
}

// The stalls on the side `sign` of the aisle (1 south, -1 north), under the loft.
void stalls(Sketch &sketch, int sign)
{
    const auto z = [sign](int cells) { return sign * cells; };
    sketch.paint({{-38, 0, z(6)}, {38, 0, z(28)}}, Paint::Soil);
    sketch.paint({{-38, 0, z(5)}, {-29, 0, z(28)}}, Paint::Rough);
    for (int stall = 0; stall < 6; ++stall) {
        const int x = -28 + stall * 11;
        sketch.paint({{x, 1, z(5)}, {x, 5, z(28)}}, Paint::Brick);
        sketch.paint({{x + 1, 1, z(5)}, {x + 10, 1, z(5)}}, Paint::Wall, Side::East);
        if (stall % 2 == 0 || sketch.roll() < 0.5) {
            sketch.resident({x + 5, 1, z(16)}, Folk::Beast);
        }
    }
    sketch.paint({{38, 1, z(5)}, {38, 5, z(28)}}, Paint::Brick);
    sketch.paint({{-38, 6, z(5)}, {38, 6, z(28)}}, Paint::Brick);
    sketch.paint({{-38, 7, z(5)}, {38, 7, z(5)}}, Paint::Wall, Side::East);
    sketch.addPart({{-38, 0, z(5)}, {38, 7, z(28)}});
}

void stablesCorners(Sketch &sketch)
{
    for (const int x : {-40, 36}) {
        for (const int z : {-30, 26}) {
            const QcBlockBox corner{{x - 2, 0, z - 2}, {x + 6, 16, z + 6}};
            sketch.paint({corner.low.offset(0, -1, 0), {corner.high.x, -1, corner.high.z}}, Paint::Foundation);
            sketch.paint(corner, Paint::Brick);
            weather(sketch, corner);
        }
    }
}

void stables(Sketch &sketch)
{
    sketch.yard({{-40, 0, -30}, {40, 10, 30}}, 2);
    stalls(sketch, 1);
    stalls(sketch, -1);
    sketch.flight({-36, 0, 5}, Side::South, houseStorey, 3);
    sketch.flight({-36, 0, -5}, Side::North, houseStorey, 3);

    // The lava trough down the aisle, with a crossing in its middle.
    sketch.paint({{-30, 0, -1}, {30, 0, 1}}, Paint::Lava);
    sketch.paint({{-2, 0, -1}, {2, 0, 1}}, Paint::Polished);

    sketch.door({-40, 1, 0}, Side::West, gate);
    sketch.landing({-40, 0, 0}, Side::West);
    sketch.ruin({{-40, 8, -30}, {40, 11, 30}});
    stablesCorners(sketch);

    sketch.chest({33, 7, 24}, Side::North, Loot::Stables);
    sketch.chest({33, 7, -24}, Side::South, Loot::Stables);
    sketch.chest({-20, 7, 24}, Side::North, Loot::Other);
    for (const QcBlockPos &guard : {QcBlockPos{-20, 1, -3}, {10, 1, 3}, {26, 1, -3}}) {
        sketch.resident(guard, Folk::Guard);
    }
    sketch.resident({0, 7, 14}, Folk::Bruiser);
    sketch.resident({0, 7, -14}, Folk::Bruiser);
    sketch.sign({-43, 7, 0}, u"structure.qtcraft.bastion_stables"_s, true);
    sketch.sign({-5, 4, 12}, u"mob.qtcraft.bristleboar"_s);
}

// The hall's floors: a ring around a sea of lava, a balcony over it and the pillar.
void treasureFloors(Sketch &sketch)
{
    sketch.paint({{-23, 1, -23}, {23, 3, 23}}, Paint::Brick);
    sketch.paint({{-19, 1, -19}, {19, 3, 19}}, Paint::Lava);
    sketch.paint({{-6, 1, -6}, {6, 16, 6}}, Paint::Brick);
    sketch.paint({{-6, 14, -6}, {6, 15, 6}}, Paint::Gilded);
    sketch.paint({{-23, 16, -23}, {23, 16, 23}}, Paint::Brick);
    sketch.paint({{-19, 16, -19}, {19, 16, 19}}, Paint::Air);
    sketch.paint({{-6, 16, -6}, {6, 16, 6}}, Paint::Chiseled);
    sketch.paint({{-3, 17, -3}, {3, 17, 3}}, Paint::Gold);
    for (const QcBlockPos &corner : {QcBlockPos{-6, 17, -6}, {6, 17, -6}, {-6, 17, 6}, {6, 17, 6}}) {
        lanternPost(sketch, corner);
    }
}

// The four bridges over the lava from the balcony to the pillar.
void treasureBridges(Sketch &sketch)
{
    sketch.deck({{-19, 16, -2}, {-7, 21, 2}}, Side::East);
    sketch.deck({{7, 16, -2}, {19, 21, 2}}, Side::East);
    sketch.deck({{-2, 16, -19}, {2, 21, -7}}, Side::South);
    sketch.deck({{-2, 16, 7}, {2, 21, 19}}, Side::South);
}

void treasure(Sketch &sketch)
{
    constexpr QcBlockBox hall{{-24, 0, -24}, {24, 30, 24}};
    sketch.hall(hall);
    treasureFloors(sketch);
    treasureBridges(sketch);
    sketch.flight({-23, 3, 18}, Side::North, 13, 3);
    weather(sketch, hall);

    // The way in: up to the ring, over the steps before the south wall.
    sketch.paint({{-2, -1, 25}, {2, -1, 29}}, Paint::Foundation);
    sketch.paint({{-2, 1, 26}, {2, 7, 29}}, Paint::Air);
    sketch.paint({{-2, 4, 25}, {2, 7, 25}}, Paint::Air);
    sketch.paint({{-2, 0, 25}, {2, 3, 25}}, Paint::Brick);
    sketch.paint({{-2, 0, 26}, {2, 0, 29}}, Paint::Rough);
    sketch.flight({-2, 0, 28}, Side::North, 3, 5);
    sketch.door({0, 4, 24}, Side::South);
    sketch.landing({0, 0, 29}, Side::South);

    sketch.chest({0, 18, 0}, Side::South, Loot::Treasure);
    sketch.chest({21, 4, -22}, Side::South, Loot::Other);
    sketch.chest({-21, 4, -22}, Side::South, Loot::Other);
    sketch.spawner({-22, 4, 22});
    sketch.spawner({22, 4, 22});
    for (const QcBlockPos &bruiser : {QcBlockPos{0, 18, 3}, {12, 17, 0}, {-12, 17, 0}}) {
        sketch.resident(bruiser, Folk::Bruiser);
    }
    for (const QcBlockPos &guard : {QcBlockPos{21, 4, 0}, {-22, 4, -8}, {0, 4, -22}, {21, 17, 21}, {-21, 17, -21}}) {
        sketch.resident(guard, Folk::Guard);
    }
    sketch.sign({0, 7, 31}, u"structure.qtcraft.bastion_treasure"_s, true);
    sketch.sign({0, 20, 0}, u"structure.qtcraft.bastion_chest"_s);
    sketch.sign({-22, 6, 22}, u"block.qtcraft.spawner"_s);
}

// The side of the house around `middle` that faces the middle of the quarter.
Side inward(int x, int z)
{
    if (x < 0) {
        return Side::East;
    }
    if (x > 0) {
        return Side::West;
    }
    return z < 0 ? Side::South : Side::North;
}

// A house of `storeys` storeys around `middle`.
void house(Sketch &sketch, QcBlockPos middle, int storeys)
{
    const int x = middle.x;
    const int z = middle.z;
    const int half = 11;
    tower(sketch, {{x - half, 0, z - half}, {x + half, storeys * houseStorey + 1, z + half}}, houseStorey);
    for (int storey = 0; storey + 1 < storeys; ++storey) {
        const bool north = storey % 2 == 0;
        sketch.flight({x - 8, storey * houseStorey, z + (north ? -9 : 7)}, Side::East, houseStorey, 3);
    }
    const Side door = x == 0 && z == 0 ? Side::West : inward(x, z);
    sketch.door(QcFortressPlan::onSide({x, 1, z}, door, half, 0), door);
}

// The windows of the upper storeys of the house around `middle`, on two sides.
void windows(Sketch &sketch, QcBlockPos middle, int storeys)
{
    for (int storey = 1; storey < storeys; ++storey) {
        const int y = storey * houseStorey + 2;
        sketch.paint({{middle.x - 3, y, middle.z - 11}, {middle.x + 3, y + 1, middle.z - 11}}, Paint::Wall, Side::East);
        sketch.paint({{middle.x - 3, y, middle.z + 11}, {middle.x + 3, y + 1, middle.z + 11}}, Paint::Wall, Side::East);
    }
}

// The houses of the quarter on a lattice three by three.
void houses(Sketch &sketch)
{
    for (const int z : {-28, 0, 28}) {
        for (const int x : {-28, 0, 28}) {
            const bool corner = x != 0 && z != 0;
            const int storeys = corner ? 2 : 3;
            const QcBlockPos middle{x, 0, z};
            house(sketch, middle, storeys);
            windows(sketch, middle, storeys);
            if (corner && sketch.roll() < 0.3) {
                sketch.ruin({{x - 11, 7, z - 11}, {x + 11, 2 * houseStorey + 3, z + 11}});
            }
            sketch.resident({x + 4, 1, z + 1}, Folk::Guard);
            if (sketch.roll() < 0.5 || (x == 0 && z != 0)) {
                sketch.chest({x + 4, 1, z - 9}, Side::South, Loot::Other);
            }
        }
    }
}

// The bridges on the third floors, from the middle house to the four around it.
void houseBridges(Sketch &sketch)
{
    constexpr int floor = 2 * houseStorey;
    sketch.deck({{-16, floor, -1}, {-12, floor + 4, 1}}, Side::East);
    sketch.deck({{12, floor, -1}, {16, floor + 4, 1}}, Side::East);
    sketch.deck({{-1, floor, -16}, {1, floor + 4, -12}}, Side::South);
    sketch.deck({{-1, floor, 12}, {1, floor + 4, 16}}, Side::South);
    for (const auto &[at, side] : {std::pair{QcBlockPos{-11, floor + 1, 0}, Side::West}, {{11, floor + 1, 0}, Side::East}, {{0, floor + 1, -11}, Side::North}, {{0, floor + 1, 11}, Side::South}}) {
        sketch.door(at, side);
        sketch.door(QcFortressPlan::onSide(at, side, 6, 0), QcFortressPlan::opposite(side));
    }
}

void housing(Sketch &sketch)
{
    sketch.yard({{-44, 0, -44}, {44, 7, 44}}, 2);
    sketch.paint({{-42, 8, -42}, {42, 22, 42}}, Paint::Air);
    houses(sketch);
    houseBridges(sketch);
    for (const int x : {-14, 14}) {
        for (const int z : {-14, 14}) {
            sketch.paint({{x - 1, 0, z - 1}, {x + 1, 0, z + 1}}, Paint::Lava);
        }
    }
    sketch.door({-44, 1, 0}, Side::West, gate);
    sketch.landing({-44, 0, 0}, Side::West);
    sketch.ruin({{-44, 5, -44}, {44, 8, -40}});
    sketch.ruin({{-44, 5, 40}, {44, 8, 44}});
    sketch.ruin({{40, 5, -44}, {44, 8, 44}});

    sketch.chest({-6, 2 * houseStorey + 1, 8}, Side::North, Loot::Other);
    sketch.resident({6, houseStorey + 1, 0}, Folk::Bruiser);
    sketch.resident({-6, 2 * houseStorey + 1, 0}, Folk::Bruiser);
    sketch.resident({28, houseStorey + 1, 0}, Folk::Guard);
    sketch.resident({0, houseStorey + 1, -28}, Folk::Guard);
    sketch.sign({-47, 7, 0}, u"structure.qtcraft.bastion_housing"_s, true);
    sketch.sign({-14, 2 * houseStorey + 3, 0}, u"mob.qtcraft.tuskling_bruiser"_s);
}

} // namespace

namespace QcBastionForms {

void draw(QcBastionSketch &sketch, QcBastionPlan::Form form)
{
    switch (form) {
    case QcBastionPlan::Form::Bridge:
        bridge(sketch);
        break;
    case QcBastionPlan::Form::Stables:
        stables(sketch);
        break;
    case QcBastionPlan::Form::Treasure:
        treasure(sketch);
        break;
    case QcBastionPlan::Form::Housing:
        housing(sketch);
        break;
    }
    sketch.finish();
}

} // namespace QcBastionForms

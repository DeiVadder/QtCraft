// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/terrain/QcDeltaSites.h"

#include <array>

/*! \brief The arms of a river delta (decision E-850): a network that branches from the river at the
    fan's apex two or three levels deep towards the sea, with a few side arms that leave an arm and
    join it again around an island.

    Every arm meanders, varies in width and grows narrower and shallower towards the sea; whether
    an arm is there, where it branches, how far it strays and how it bends come from the delta's
    traits, so every column of a delta sees the same network. Arms are given in the fan's frame:
    along it (0 at the apex, 1 at the mouth) and across it in blocks. */
class QcDeltaArms
{
public:
    explicit QcDeltaArms(const QcDeltaSites::Site &site);

    /*! Returns the ground of the arms at \a along, \a lateral blocks across the fan where its
        half width is \a halfWidth: under the arms down to their beds, rising over their banks;
        \a edge is the height of the banks at the water line. */
    [[nodiscard]] double channelAt(double along, double lateral, double halfWidth, double edge) const;

private:
    // An arm: its parent (an earlier arm, -1 for the river), where it leaves the parent (along) and
    // over how much it strays to its offset (a share of the fan's half width, signed), where it ends
    // (`until`, the river at its first branching) or joins its parent again (`rejoin`, 0 for never),
    // its half width in blocks, how far (blocks) and how often (per fan length) it meanders, and the
    // phase of its bends and of its width.
    struct Arm
    {
        int parent = -1;
        double start = 0.0;
        double ramp = 0.0;
        double offset = 0.0;
        double until = 1.0e9;
        double rejoin = 0.0;
        double width = 0.0;
        double wave = 0.0;
        double frequency = 0.0;
        double phase = 0.0;
    };
    static constexpr int maxArms = 24;

    // The next trait of the delta (0..1); the traits are taken in a fixed order.
    [[nodiscard]] double nextTrait();
    // Adds an arm (none beyond maxArms) and returns its index; the bends come from the traits.
    int add(Arm arm);
    // The first level: two or three arms from the river, each with side arms.
    void addFirstLevel(double split, double riverWidth);
    // The branches of the first-level arm `parent` (offset `side`, signed share of the half width).
    void addBranches(int parent, double side, double riverWidth);
    // Where the middle of arm `index` lies across the fan at `along`, given its parent's.
    [[nodiscard]] double middleOf(const Arm &arm, double parentMiddle, double along, double halfWidth) const;
    // Half the width of `arm` at `along` in blocks.
    [[nodiscard]] static double widthOf(const Arm &arm, double along);

    const QcDeltaSites::Site &m_site;
    int m_trait = 0;
    std::array<Arm, maxArms> m_arms{};
    int m_count = 0;
};

// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/world/QcBlockAccess.h"

#include <QRandomGenerator>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

class QcServerWorld;

/*! \brief Random-tick rules of farming (decisions E-289 to E-291), called by QcPlantGrowth for
    blocks with \c {crop} or \c {soil}.

    A crop advances one stage with a chance that gives its \c {averageSeconds} from the first to
    the ripe stage on wet soil (times \c {dryFactor} elsewhere) and only in enough light; a ripe
    crop with a fruit (stems) puts that fruit onto a free neighbouring cell. Soil turns wet near
    water or in the rain (the cell above it under the open rain, setRainCheck, decision E-396),
    dries step by step without either and reverts (to dirt) when dry and bare or covered by a
    full block. */
class QcCropGrowth
{
public:
    /*! Sets a block and tells the clients; returns false when it could not be set. */
    using SetBlock = std::function<bool(QcBlockPos pos, QcBlockId block)>;
    /*! Whether rain falls on a position (weather). */
    using RainCheck = std::function<bool(QcBlockPos pos)>;

    /*! Reads the crops and soils of \a content; \a randomTickSeconds: average seconds between
        two random ticks of one block. */
    QcCropGrowth(std::shared_ptr<const QcContentRegistry> content, double randomTickSeconds);

    /*! Runs one random tick of the crop or soil \a block at \a pos of \a world, drawn from
        \a random, setting blocks with \a setBlock. Returns \c true when it changed the world. */
    bool randomTick(const QcServerWorld &world, QcBlockPos pos, QcBlockId block, QRandomGenerator &random,
                    const SetBlock &setBlock) const;

    /*! Returns the chance that a random tick advances \a crop by one stage (\a wet: on wet
        soil). */
    double stageChance(QcBlockId crop, bool wet) const;
    /*! Returns the chance that a random tick of the ripe stem \a crop puts its fruit down
        (\a wet: on wet soil). */
    double fruitChance(QcBlockId crop, bool wet) const;
    /*! Returns whether the block of \a world below \a pos is soil in its wettest state. */
    bool isOnWetSoil(const QcBlockAccess &world, QcBlockPos pos) const;
    /*! \brief Whether water lies near soil. */
    enum class Water {
        /*! Water lies near. */
        Near,
        /*! No water lies near. */
        None,
        /*! None was found, but part of the area is not loaded. */
        Unknown
    };
    /*! Returns whether a block of \a world named \c {soil.water} lies within \c {soil.reach}
        blocks of \a pos horizontally (\a soil), at the same level or one above (searched from
        the centre outwards). Unknown when none was found but part of the area is not loaded:
        soil at a chunk border then waits instead of drying. */
    Water waterNear(const QcBlockAccess &world, QcBlockPos pos, const QcSoilDefinition &soil) const;
    /*! Returns the light at \a pos of \a world for plants: the brighter of block light and sky
        light of the world's light engine (decision E-389; sky light at full strength whatever
        the time of day). */
    static int plantLightAt(const QcServerWorld &world, QcBlockPos pos);

    /*! Sets the \a check of where rain falls (rain wets soil). */
    void setRainCheck(RainCheck check) { m_rainsOn = std::move(check); }

    /*! Returns the state of soil block \a soil freshly made at \a pos of \a world (a tilled
        block): wet when water is near. */
    QcBlockId freshSoil(const QcBlockAccess &world, QcBlockPos pos, QcBlockId soil) const;

private:
    bool tickCrop(const QcServerWorld &world, QcBlockPos pos, QcBlockId block, QRandomGenerator &random,
                  const SetBlock &setBlock) const;
    bool tickSoil(const QcServerWorld &world, QcBlockPos pos, QcBlockId block, const SetBlock &setBlock) const;
    // Puts the fruit of a ripe stem at `pos` onto a random free neighbour; false when there is none.
    bool putFruit(const QcBlockAccess &world, QcBlockPos pos, const QcCropDefinition &crop, QRandomGenerator &random,
                  const SetBlock &setBlock) const;
    // Index of the value of `property` in state `block` among the property's values; -1 if missing.
    int stageOf(QcBlockId block, const QString &property) const;
    int stageCount(QcBlockId block, const QString &property) const;

    std::shared_ptr<const QcContentRegistry> m_content;
    double m_randomTickSeconds = 1.0;
    RainCheck m_rainsOn;
    // Horizontal offsets within each reach (0..maxReach), nearest first.
    std::array<std::vector<std::pair<int, int>>, 9> m_ringOffsets;
};

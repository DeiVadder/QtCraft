// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/physics/QcPhysics.h"
#include "common/world/QcBlockAccess.h"
#include "server/core/QcServerLimits.h"
#include "server/plants/QcCropGrowth.h"

#include <QRandomGenerator>
#include <QSet>

#include <functional>
#include <memory>
#include <optional>

class QcChunk;
class QcChunkSection;
class QcServerWorld;

/*! \brief Blocks with random ticks: plants that grow upwards by themselves (blocks with
    \c {growth}, e.g. sugar cane), crops and soil (QcCropGrowth).

    Every server tick picks QcServerLimits::randomTicksPerSection random blocks in each loaded
    section that may hold such a block (QcServerWorld::marks, QcBlockTrait::RandomTicks). A
    ticked top block of a plant column grows one block of its kind with the chance that gives its
    \c {averageSeconds} per step, while the column is below \c {maxHeight}, the block above is
    air and the column's base still has its ground (cane: water next to it). Random ticks run
    only in loaded chunks; time a chunk spent unloaded is not made up (decision E-289). */
class QcPlantGrowth
{
public:
    /*! Random ticks per marked section and tick by default
        (QcServerLimits::randomTicksPerSection). */
    static constexpr int randomTicksPerSection = static_cast<int>(QcServerLimits{}.randomTicksPerSection);
    /*! Average seconds between two random ticks of one block at the default; the growth chances
        count with it, so more random ticks make plants grow faster. */
    static constexpr double randomTickSeconds = 4096.0 / randomTicksPerSection * QcPhysics::tickDuration;

    /*! Sets a block and tells the clients; returns false when it could not be set. */
    using SetBlock = std::function<bool(QcBlockPos pos, QcBlockId block)>;

    QcPlantGrowth(std::shared_ptr<const QcContentRegistry> content, quint64 seed);

    /*! Runs the random ticks of one server tick in the marked sections of \a world, setting
        blocks with \a setBlock. */
    void tick(const QcServerWorld &world, const SetBlock &setBlock);
    /*! Sets the random \a ticks per marked section and tick. */
    void setRandomTicksPerSection(int ticks) { m_randomTicksPerSection = ticks; }
    /*! Runs one random tick of the block at \a pos of \a world, setting blocks with
        \a setBlock. Returns \c true when it changed the world. */
    bool randomTick(const QcServerWorld &world, QcBlockPos pos, const SetBlock &setBlock);
    /*! Returns the air block of \a world a plant at \a pos would grow into, if its column may
        grow now. */
    [[nodiscard]] std::optional<QcBlockPos> growthTarget(const QcBlockAccess &world, QcBlockPos pos) const;
    /*! Returns the chance that a random tick of \a block grows it. */
    double growthChance(QcBlockId block) const;
    /*! Returns the rules of crops and soil. */
    const QcCropGrowth &crops() const { return m_crops; }
    /*! Sets the \a check of where rain falls: rain wets soil (QcCropGrowth::setRainCheck). */
    void setRainCheck(QcCropGrowth::RainCheck check) { m_crops.setRainCheck(std::move(check)); }

private:
    bool mayHoldPlants(const QcChunkSection &section) const;
    // Picks the random cells of the sections of `chunk` marked in `sections` (bit n: section n) and
    // calls `visit(pos)` for those holding a growing plant.
    template<typename Visit>
    void forRandomPlants(const QcChunk &chunk, quint32 sections, Visit visit);

    std::shared_ptr<const QcContentRegistry> m_content;
    QRandomGenerator m_random;
    QcCropGrowth m_crops;
    int m_randomTicksPerSection = randomTicksPerSection;
    // Every state id of every block with random ticks.
    QSet<QcBlockId> m_growing;
};

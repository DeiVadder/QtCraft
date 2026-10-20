#pragma once

#include "clientcore/session/QcClientWorld.h"
#include "common/core/QcVec3.h"

#include <QList>

#include <vector>

// The smoke rising from the lit campfires near the player (decision E-772): a list of the campfires
// (blocks whose campfire cooks) within `radius` blocks, found again every `rescanTicks` by looking only
// into the chunk sections whose palette holds such a block, and a few puffs per campfire that rise,
// drift and shrink in a loop. The puffs follow from the campfire's position and the time alone, so
// nothing is kept per puff.
class QcCampfireSmoke
{
public:
    // A puff as the entity frame draws it: a small cube of smoke.
    struct Puff
    {
        QcVec3 position;
        double size = 0.0;
    };

    static constexpr int radius = 32;
    static constexpr int rescanTicks = 10;
    static constexpr int puffsPerCampfire = 5;
    // Seconds a puff takes from the fire to where it fades.
    static constexpr double puffSeconds = 3.0;
    // Campfires drawn with smoke at most (the nearest).
    static constexpr int maxCampfires = 64;

    // Looks for the campfires again when `tick` is rescanTicks past the last look or the world changed.
    void refresh(const QcClientWorld &world, const QcVec3 &eye, quint64 tick);
    const QList<QcBlockPos> &campfires() const { return m_campfires; }
    // The puffs of the campfire at `pos` at `seconds` of world time.
    [[nodiscard]] static QList<Puff> puffsOf(QcBlockPos pos, double seconds);

private:
    // The campfires of the sections near `eye`.
    [[nodiscard]] QList<QcBlockPos> scan(const QcClientWorld &world, const QcVec3 &eye) const;
    // The campfires in one section whose first cell is at `origin`; none without one in its palette.
    [[nodiscard]] QList<QcBlockPos> campfiresIn(const QcChunkSection &section, QcBlockPos origin) const;
    // Whether the block state smokes.
    [[nodiscard]] bool smokes(QcBlockId id) const;
    // Whether a block state smokes (a cooking campfire), per state id of the world's content.
    void learn(const QcContentRegistry &content);

    std::vector<bool> m_smokes;
    const QcContentRegistry *m_content = nullptr;
    QList<QcBlockPos> m_campfires;
    quint64 m_scannedAt = 0;
    bool m_scanned = false;
};

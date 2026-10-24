#pragma once

#include "common/core/QcVec3.h"
#include "common/protocol/QcWorldMessages.h"

#include <QList>

#include <optional>

// The flash and smoke of the explosions the server told about (QcMsg::Explosion, decision E-604):
// each explosion leaves a few bright puffs of fire that burst out and fade fast and more puffs of
// smoke that drift up and shrink, all as small cubes the entity frame draws. Directions and sizes
// come from the explosion's position, so the same explosion looks the same; the number of puffs grows
// with the power and is bounded, and so is the number of puffs in all. Near the eye an explosion also
// lights the world in a short warm flash and shakes the camera (decision E-879).
class QcExplosionEffects
{
public:
    struct Puff
    {
        QcVec3 centre;
        // Metres per second of its burst.
        QcVec3 velocity;
        quint64 startTick = 0;
        int lifeTicks = 20;
        double size = 1.0;
        bool flash = false;
    };

    // What a puff looks like `ticks` after the explosion (with the frame's fraction of a tick).
    struct Look
    {
        QcVec3 position;
        double size = 0.0;
        bool flash = false;
    };

    // Camera offsets of a shake, in degrees.
    struct Shake
    {
        double yaw = 0.0;
        double pitch = 0.0;
    };

    static constexpr int flashPuffs = 10;
    static constexpr int smokePuffsPerPower = 7;
    // How long an explosion lights the world and shakes the camera.
    static constexpr int flashTicks = 4;
    static constexpr int shakeTicks = 24;
    // Metres per unit of power within which the flash shows and the camera shakes.
    static constexpr double flashReachPerPower = 10.0;
    static constexpr double shakeReachPerPower = 6.0;
    // Largest offsets of a full-strength shake.
    static constexpr double maxShakeDegrees = 2.5;
    static constexpr int maxPuffs = 512;
    static constexpr qsizetype maxBlasts = 64;
    // Content textures of the puffs (atlas names with the namespace); the campfire smoke
    // (QcCampfireSmoke) is drawn with the smoke texture too.
    static constexpr auto flashTexture = "qtcraft:explosion_flash";
    static constexpr auto smokeTexture = "qtcraft:explosion_smoke";

    void add(const QcMsg::Explosion &explosion, quint64 tick);
    // Forgets the puffs and blasts that are over at `tick`.
    void prune(quint64 tick);
    const QList<Puff> &puffs() const { return m_puffs; }
    // Nothing while the puff is over.
    static std::optional<Look> lookAt(const Puff &puff, double ticks);
    // Brightness (0..1) of the explosions' flash seen from `eye` at `ticks` (with the frame's fraction).
    [[nodiscard]] double flashAt(const QcVec3 &eye, double ticks) const;
    // Strength (0..1) of the shake the explosions give a camera at `eye` at `ticks`.
    [[nodiscard]] double shakeAt(const QcVec3 &eye, double ticks) const;
    // The camera offsets of a shake of `strength` at `ticks`: a quick, irregular tremble.
    [[nodiscard]] static Shake shake(double strength, double ticks);

private:
    // An explosion while its flash and shake last.
    struct Blast
    {
        QcVec3 centre;
        double power = 1.0;
        quint64 tick = 0;
    };

    QList<Puff> m_puffs;
    QList<Blast> m_blasts;
};

// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/weather/QcAtmosphereParticles.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/physics/QcPhysics.h"
#include "common/world/QcBlockAccess.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace {

// Looks by kind (QcParticleKind order). Colours of the role model where it has them: crimson
// spores 0.9/0.4/0.5, white ash 0.73/0.69/0.76, dripping lava 1/0.29/0.08 and the obsidian's tear
// 0.51/0.03/0.89; the warped spores are lighter than the role model's dark blue so that they show
// in the purple fog, the soul is our own. Drops are drawn 1.75 times a flake's size (our own) so
// that they show as drops, not specks.
constexpr auto looks = std::to_array<QcParticleLook>({
    {0.22f, 0.21f, 0.21f, 0.0f, 10, 50, 0.45, 0.15, 0},
    {0.73f, 0.69f, 0.76f, 0.55f, 20, 100, 0.25, 0.6, 0},
    {0.9f, 0.4f, 0.5f, 0.6f, 20, 100, 0.12, 0.25, 0},
    {0.25f, 0.7f, 0.75f, 0.6f, 20, 100, 0.06, 0.25, 0},
    {0.55f, 0.85f, 0.95f, 1.0f, 20, 40, -0.6, 0.15, 0},
    {1.0f, 0.2857f, 0.0833f, 1.0f, 200, 200, 0.0, 0.0, 40, 3},
    {0.5117f, 0.0313f, 0.8906f, 1.0f, 200, 200, 0.0, 0.0, 100, 3},
});
static_assert(looks.size() == qcParticleKindCount);

// A drop starts this far below or beside its block.
constexpr double dropGap = 0.05;
// Share of a particle's life over which it fades in and out.
constexpr double fadeIn = 0.1;
constexpr double fadeOut = 0.25;

// Whether the block `id` (none: not loaded) leaves its cell free: loaded, no liquid, no collision.
bool isFreeBlock(const QcBlockAccess &world, std::optional<QcBlockId> id)
{
    if (!id || !world.content().isValidBlock(*id)) {
        return false;
    }
    return !world.content().block(*id).liquid && world.content().state(*id).collision.isEmpty();
}

bool isFree(const QcBlockAccess &world, QcBlockPos pos)
{
    return isFreeBlock(world, world.blockAt(pos));
}

bool isOpaque(const QcBlockAccess &world, QcBlockPos pos)
{
    const std::optional<QcBlockId> id = world.blockAt(pos);
    return id && world.content().isValidBlock(*id) && world.content().state(*id).opaque;
}

bool isSolid(const QcBlockAccess &world, const QcVec3 &position)
{
    const QcBlockPos cell{static_cast<int>(std::floor(position.x)), static_cast<int>(std::floor(position.y)), static_cast<int>(std::floor(position.z))};
    const std::optional<QcBlockId> id = world.blockAt(cell);
    if (!id || !world.content().isValidBlock(*id)) {
        return true;
    }
    return world.content().block(*id).liquid || !world.content().state(*id).collision.isEmpty();
}

// A cell offset of up to `radius` along one axis, nearer ones likelier (the role model's draw).
int offsetIn(QRandomGenerator &random, int radius)
{
    return random.bounded(radius) - random.bounded(radius);
}

} // namespace

QcAtmosphereParticles::QcAtmosphereParticles(int budget, quint32 seed)
    : m_budget(budget)
    , m_random(seed)
{
}

QcParticleLook QcAtmosphereParticles::look(QcParticleKind kind)
{
    return looks[static_cast<std::size_t>(kind)];
}

void QcAtmosphereParticles::update(const QcBlockAccess &world, const QcBiomeAtmosphere &atmosphere, const QcVec3 &eye, double seconds)
{
    move(world, eye, seconds);

    setPending(std::min(pending() + seconds, maxTicksPerUpdate * QcPhysics::tickDuration));
    while (pending() >= QcPhysics::tickDuration) {
        setPending(pending() - QcPhysics::tickDuration);
        spawnTick(world, atmosphere, eye);
    }
}

void QcAtmosphereParticles::spawnTick(const QcBlockAccess &world, const QcBiomeAtmosphere &atmosphere, const QcVec3 &eye)
{
    const QcBlockPos centre{static_cast<int>(std::floor(eye.x)), static_cast<int>(std::floor(eye.y)), static_cast<int>(std::floor(eye.z))};
    // The corners of the far cube lie beyond keepDistance; what spawned there would end at once.
    const auto keep = static_cast<int>(keepDistance * keepDistance);
    for (const auto &[samples, radius] : {std::pair{nearSamples, nearRadius}, std::pair{farSamples, farRadius}}) {
        for (int i = 0; i < samples && particles().size() < m_budget; ++i) {
            const int dx = offsetIn(random(), radius);
            const int dy = offsetIn(random(), radius);
            const int dz = offsetIn(random(), radius);
            if (dx * dx + dy * dy + dz * dz <= keep) {
                spawnIn(world, atmosphere, centre.offset(dx, dy, dz));
            }
        }
    }
}

void QcAtmosphereParticles::spawnIn(const QcBlockAccess &world, const QcBiomeAtmosphere &atmosphere, QcBlockPos pos)
{
    if (!pos.isInsideWorldHeight()) {
        return;
    }
    // One lookup serves the biome's particles and the block's.
    const std::optional<QcBlockId> id = world.blockAt(pos);
    if (!atmosphere.particles.isEmpty() && isFreeBlock(world, id)) {
        for (const QcParticleChance &particle : atmosphere.particles) {
            if (unit() < particle.chance) {
                add(particle.kind, {pos.x + unit(), pos.y + unit(), pos.z + unit()});
            }
        }
    }
    if (id && world.content().isValidBlock(*id)) {
        giveOffAll(world, *id, pos);
    }
}

void QcAtmosphereParticles::sampleBlock(const QcBlockAccess &world, QcBlockPos pos)
{
    const std::optional<QcBlockId> id = world.blockAt(pos);
    if (id && world.content().isValidBlock(*id)) {
        giveOffAll(world, *id, pos);
    }
}

void QcAtmosphereParticles::giveOffAll(const QcBlockAccess &world, QcBlockId id, QcBlockPos pos)
{
    for (const QcParticleEmitter &emitter : world.content().block(id).particles) {
        if (unit() < emitter.chance) {
            giveOff(world, emitter, pos);
        }
    }
}

void QcAtmosphereParticles::giveOff(const QcBlockAccess &world, const QcParticleEmitter &emitter, QcBlockPos pos)
{
    using From = QcParticleEmitter::From;
    // Somewhere on a face, not right at its edge.
    const double u = 0.1 + 0.8 * unit();
    const double v = 0.1 + 0.8 * unit();
    switch (emitter.from) {
    case From::Top:
        if (isFree(world, pos.offset(0, 1, 0))) {
            add(emitter.kind, {pos.x + u, pos.y + 1.0 + dropGap, pos.z + v});
        }
        return;
    case From::Bottom:
        if (isFree(world, pos.offset(0, -1, 0))) {
            add(emitter.kind, {pos.x + u, pos.y - dropGap, pos.z + v});
        }
        return;
    case From::Through:
        // Through a full block into a free cell, as liquids soak through a ceiling.
        if (isOpaque(world, pos.offset(0, -1, 0)) && isFree(world, pos.offset(0, -2, 0))) {
            add(emitter.kind, {pos.x + u, pos.y - 1.0 - dropGap, pos.z + v});
        }
        return;
    case From::Faces:
        break;
    }

    // A random face; none from the top, and none where the neighbour covers it.
    static constexpr std::array<std::array<int, 3>, 6> faces = {{{0, 1, 0}, {0, -1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}}};
    const std::array<int, 3> &face = faces[static_cast<std::size_t>(random().bounded(6))];
    const QcBlockPos neighbour = pos.offset(face[0], face[1], face[2]);
    if (face[1] > 0 || isOpaque(world, neighbour)) {
        return;
    }
    const auto across = [&](int axis, double inside) { return axis > 0 ? 1.0 + dropGap : (axis < 0 ? -dropGap : inside); };
    add(emitter.kind, {pos.x + across(face[0], u), pos.y + across(face[1], v), pos.z + across(face[2], face[0] != 0 ? v : u)});
}

void QcAtmosphereParticles::add(QcParticleKind kind, const QcVec3 &position)
{
    if (particles().size() >= m_budget) {
        return;
    }
    const QcParticleLook &shape = looks[static_cast<std::size_t>(kind)];
    Particle particle;
    particle.position = position;
    particle.kind = kind;
    particle.life = random().bounded(shape.shortestLife, shape.longestLife + 1) * QcPhysics::tickDuration;
    particle.hang = shape.hangTicks * QcPhysics::tickDuration;
    particle.phase = static_cast<float>(2.0 * std::numbers::pi * unit());
    particle.velocity = {0.0, -shape.sink, 0.0};
    // Taken out and put back, so the list is not shared and grows in place.
    QList<Particle> grown = particles();
    setParticles({});
    grown.append(particle);
    setParticles(std::move(grown));
}

std::optional<QcAtmosphereParticles::Particle> QcAtmosphereParticles::moved(Particle particle, const QcBlockAccess &world,
                                                                          double seconds)
{
    const QcParticleLook &shape = looks[static_cast<std::size_t>(particle.kind)];
    particle.age += seconds;
    if (shape.hangTicks <= 0) {
        const double sway = shape.sway * std::sin(particle.phase + 2.0 * particle.age);
        const double across = shape.sway * std::cos(particle.phase + 1.3 * particle.age);
        particle.position = particle.position + QcVec3{sway, 0.0, across} * seconds + particle.velocity * seconds;
        return particle;
    }

    // A drop hangs, then falls with gravity until it meets something.
    particle.hang -= seconds;
    if (particle.hang < 0.0) {
        const double drag = std::pow(dropDrag, seconds / QcPhysics::tickDuration);
        particle.velocity.y = (particle.velocity.y - dropGravity * seconds) * drag;
        particle.position = particle.position + particle.velocity * seconds;
        if (isSolid(world, particle.position)) {
            return std::nullopt;
        }
    }
    return particle;
}

void QcAtmosphereParticles::move(const QcBlockAccess &world, const QcVec3 &eye, double seconds)
{
    const double keep = keepDistance * keepDistance;
    // Taken out and put back, so the list is not shared: moved and compacted in place.
    QList<Particle> moving = particles();
    setParticles({});
    for (Particle &particle : moving) {
        const std::optional<Particle> next = moved(particle, world, seconds);
        if (next) {
            particle = *next;
        } else {
            // A drop that met something has lived its life.
            particle.age = particle.life;
        }
    }
    moving.removeIf([&eye, keep](const Particle &particle) {
        const QcVec3 d = particle.position - eye;
        return particle.age >= particle.life || d.x * d.x + d.y * d.y + d.z * d.z > keep;
    });
    setParticles(std::move(moving));
}

QcPrecipitationDrop QcAtmosphereParticles::dropOf(const Particle &particle, const QcVec3 &origin)
{
    const double share = particle.life > 0.0 ? particle.age / particle.life : 1.0;
    const double fade = std::clamp(std::min(share / fadeIn, (1.0 - share) / fadeOut), 0.0, 1.0);
    // Drops stay whole while they hang and fall.
    const QcParticleLook &shape = looks[static_cast<std::size_t>(particle.kind)];
    const bool drop = shape.hangTicks > 0;
    QcVec3 relative = particle.position - origin;
    if (drop) {
        // The renderer draws a dot above its point; a drop hangs below it.
        relative.y -= QcPrecipitationField::flakeSize * (1.0 + shape.grow / 4.0);
    }
    QcPrecipitationDrop shown;
    shown.x = static_cast<float>(relative.x);
    shown.y = static_cast<float>(relative.y);
    shown.z = static_cast<float>(relative.z);
    shown.snow = 1;
    shown.alpha = static_cast<quint8>(std::lround(255.0 * (drop ? 1.0 : fade)));
    shown.palette = static_cast<quint8>(1 + static_cast<int>(particle.kind));
    shown.grow = static_cast<quint8>(shape.grow);
    return shown;
}

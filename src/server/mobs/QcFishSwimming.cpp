#include "server/mobs/QcFishSwimming.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/physics/QcPhysics.h"
#include "server/entities/QcCreature.h"
#include "server/mobs/QcMobSteering.h"

#include <algorithm>
#include <cmath>
#include <numbers>

QcFishSwimming::QcFishSwimming(QRandomGenerator &random)
    : m_random(random)
{
}

qint64 QcFishSwimming::cellKey(int x, int y, int z)
{
    // 21 bits per axis: worlds are far smaller than a million cells of a metre or more.
    const auto bits = [](int value) { return static_cast<qint64>(value) & 0x1FFFFF; };
    return (bits(x) << 42) | (bits(y) << 21) | bits(z);
}

double QcFishSwimming::cellSizeOf(const QcAquaticDefinition &aquatic)
{
    return std::max(aquatic.schoolRadius, minCellSize);
}

QcFishSwimming::CellKey QcFishSwimming::cellOf(int type, const QcVec3 &position, double size)
{
    return {type, cellKey(static_cast<int>(std::floor(position.x / size)), static_cast<int>(std::floor(position.y / size)),
        static_cast<int>(std::floor(position.z / size)))};
}

void QcFishSwimming::startTick(std::span<const QcCreature> mobs, const QcContentRegistry &content)
{
    // Emptied without giving back their storage: the grid is built anew every tick.
    m_cellIndex.removeIf([](const auto &) { return true; });
    m_cellOfMob.assign(static_cast<std::size_t>(mobs.size()), -1);
    m_cellStart.clear();

    // First the cell of each awake water creature and the size of each cell ...
    for (qsizetype i = 0; i < std::ssize(mobs); ++i) {
        const QcCreature &mob = mobs[i];
        const std::optional<QcAquaticDefinition> &aquatic = content.mobs()[mob.type].aquatic;
        if (mob.dormant || !aquatic) {
            continue;
        }
        const CellKey key = cellOf(mob.type, mob.motion.position, cellSizeOf(*aquatic));
        auto cell = m_cellIndex.find(key);
        if (cell == m_cellIndex.end()) {
            cell = m_cellIndex.insert(key, static_cast<qsizetype>(m_cellStart.size()));
            m_cellStart.push_back(0);
        }
        m_cellOfMob[static_cast<std::size_t>(i)] = *cell;
        ++m_cellStart[static_cast<std::size_t>(*cell)];
    }

    // ... then the start of each cell's run, and the creatures sorted into their runs.
    qsizetype total = 0;
    for (qsizetype &start : m_cellStart) {
        total += std::exchange(start, total);
    }
    m_cellStart.push_back(total);
    m_members.assign(static_cast<std::size_t>(total), 0);
    std::vector<qsizetype> &next = m_cellStart;
    for (qsizetype i = 0; i < std::ssize(mobs); ++i) {
        if (const qsizetype cell = m_cellOfMob[static_cast<std::size_t>(i)]; cell >= 0) {
            m_members[static_cast<std::size_t>(next[static_cast<std::size_t>(cell)]++)] = i;
        }
    }
    // Filling moved every start to the next cell's start: shift them back.
    for (std::size_t cell = m_cellStart.size() - 1; cell > 0; --cell) {
        m_cellStart[cell] = m_cellStart[cell - 1];
    }
    m_cellStart[0] = 0;
}

bool QcFishSwimming::inWater(const QcBlockAccess &world, const QcVec3 &position)
{
    return QcPhysics::isWaterAt(world, QcPhysics::blockContaining(position));
}

void QcFishSwimming::collectFrom(std::span<const QcCreature> mobs, qsizetype index, CellKey cell, double radius)
{
    const auto found = m_cellIndex.constFind(cell);
    if (found == m_cellIndex.constEnd()) {
        return;
    }

    const QcVec3 &self = mobs[index].motion.position;
    const qsizetype end = m_cellStart[static_cast<std::size_t>(*found + 1)];
    for (qsizetype member = m_cellStart[static_cast<std::size_t>(*found)]; member < end; ++member) {
        const qsizetype other = m_members[static_cast<std::size_t>(member)];
        const QcVec3 apart = mobs[other].motion.position - self;
        const double distanceSquared = apart.x * apart.x + apart.y * apart.y + apart.z * apart.z;
        if (other != index && distanceSquared <= radius * radius) {
            m_candidates.push_back({distanceSquared, &mobs[other]});
        }
    }
}

const QList<const QcCreature *> &QcFishSwimming::neighboursOf(std::span<const QcCreature> mobs, qsizetype index, const QcAquaticDefinition &aquatic)
{
    m_candidates.clear();
    m_found.clear();
    if (aquatic.neighbours <= 0 || aquatic.schoolRadius <= 0.0) {
        return m_found;
    }

    // The cells are as large as the school radius: the 27 around the creature's own hold them all.
    const QcCreature &self = mobs[index];
    const double size = cellSizeOf(aquatic);
    const QcVec3 &p = self.motion.position;
    const int cx = static_cast<int>(std::floor(p.x / size));
    const int cy = static_cast<int>(std::floor(p.y / size));
    const int cz = static_cast<int>(std::floor(p.z / size));
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                collectFrom(mobs, index, {self.type, cellKey(cx + dx, cy + dy, cz + dz)}, aquatic.schoolRadius);
            }
        }
    }

    // The nearest ones, whichever cell they are in, so that no direction is preferred.
    const auto nearer = [](Candidate a, Candidate b) { return a.distanceSquared < b.distanceSquared; };
    if (m_candidates.size() > static_cast<std::size_t>(aquatic.neighbours)) {
        std::nth_element(m_candidates.begin(), m_candidates.begin() + aquatic.neighbours, m_candidates.end(), nearer);
        m_candidates.resize(static_cast<std::size_t>(aquatic.neighbours));
    }
    for (const Candidate &candidate : m_candidates) {
        m_found.append(candidate.mob);
    }
    return m_found;
}

QcVec3 QcFishSwimming::schooling(const QcVec3 &position, const QList<const QcCreature *> &others, const QcAquaticDefinition &aquatic)
{
    if (others.isEmpty()) {
        return {};
    }

    QcVec3 centre;
    QcVec3 heading;
    QcVec3 apart;
    for (const QcCreature *other : others) {
        centre += other->motion.position;
        heading += other->motion.velocity;
        const QcVec3 away = position - other->motion.position;
        const double distance = away.length();
        if (distance > 1e-6 && distance < aquatic.separationDistance) {
            // Stronger the closer it is: 1 at contact, 0 at the separation distance.
            apart += away.normalized() * (1.0 - distance / aquatic.separationDistance);
        }
    }
    centre = centre * (1.0 / static_cast<double>(others.size()));

    QcVec3 steering;
    const QcVec3 toCentre = centre - position;
    if (aquatic.schoolRadius > 0.0) {
        steering += toCentre * (aquatic.cohesion / aquatic.schoolRadius);
    }
    if (heading.length() > 1e-6) {
        steering += heading.normalized() * aquatic.alignment;
    }
    steering += apart * aquatic.separation;
    return steering;
}

void QcFishSwimming::swim(qsizetype index, std::span<QcCreature> mobs, const QcMobDefinition &definition, const QcBlockAccess &world, double dt)
{
    QcCreature &mob = mobs[index];
    const QcAquaticDefinition &aquatic = *definition.aquatic; // NOLINT(bugprone-unchecked-optional-access): called for water creatures only
    const QcVec3 centre = mob.motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};
    mob.motion.inWater = inWater(world, centre);
    if (!mob.motion.inWater) {
        flopOnLand(mob, definition, world, dt);
        return;
    }

    mob.dryTicks = 0;
    const QcVec3 school = schooling(mob.motion.position, neighboursOf(mobs, index, aquatic), aquatic);
    swimInWater(mob, definition, school, world, dt);
}

QcVec3 QcFishSwimming::keepInWater(const QcVec3 &wish, const QcVec3 &centre, const QcBlockAccess &world)
{
    QcVec3 kept = wish;
    // Never above the surface: up only while the cell above is water too.
    if (kept.y > 0.0 && !inWater(world, centre + QcVec3{0.0, 0.6, 0.0})) {
        kept.y = -0.2;
    }
    // Turn back at the shore, the bottom and walls: what lies ahead must be water.
    const QcVec3 flat{kept.x, 0.0, kept.z};
    if (flat.length() > 1e-6 && !inWater(world, centre + flat.normalized() * 0.8)) {
        kept.x = -kept.x;
        kept.z = -kept.z;
    }
    if (kept.y < 0.0 && !inWater(world, centre + QcVec3{0.0, -0.6, 0.0})) {
        kept.y = 0.0;
    }
    return kept;
}

void QcFishSwimming::swimInWater(QcCreature &mob, const QcMobDefinition &definition, const QcVec3 &school, const QcBlockAccess &world, double dt)
{
    const QcAquaticDefinition &aquatic = *definition.aquatic; // NOLINT(bugprone-unchecked-optional-access): swim() checked it
    mob.wanderTimer -= dt;
    if (mob.wanderTimer <= 0.0 || mob.blockedLastTick) {
        mob.wanderTimer = wanderMinSeconds + m_random.bounded(wanderMaxSeconds - wanderMinSeconds);
        const double angle = m_random.bounded(2.0 * std::numbers::pi);
        mob.wanderDirection = {std::sin(angle), (m_random.bounded(1.0) - 0.5) * 0.6, -std::cos(angle)};
    }

    // A hurt fish darts away, its school follows it along.
    const bool fleeing = mob.fleeTimer > 0.0;
    // In a school it wanders less and follows the others more.
    QcVec3 wish = mob.wanderDirection * (school.length() > 0.0 ? schoolWander : 1.0) + school;
    if (wish.length() > 1.0) {
        wish = wish.normalized();
    }
    const QcVec3 centre = mob.motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};
    wish = keepInWater(wish, centre, world);
    const double speed = fleeing ? definition.chaseSpeed : aquatic.swimSpeed;

    // While knocked back it does not steer; the water slows the push.
    const double blend = std::min(1.0, (mob.hurt.knockedBack() ? 1.0 : 3.0) * dt);
    const QcVec3 target = mob.hurt.knockedBack() ? QcVec3{} : wish * speed;
    mob.motion.velocity += (target - mob.motion.velocity) * blend;

    const QcPhysics::MoveResult move =
        QcPhysics::moveBox(QcPhysics::boxAt(mob.motion.position, definition.width, definition.height), mob.motion.velocity * dt, world);
    mob.motion.position += move.applied;
    mob.motion.onGround = move.blockedY && mob.motion.velocity.y < 0.0;
    mob.blockedLastTick = move.blockedX || move.blockedY || move.blockedZ;
    if (move.blockedX) {
        mob.motion.velocity.x = 0.0;
    }
    if (move.blockedY) {
        mob.motion.velocity.y = 0.0;
    }
    if (move.blockedZ) {
        mob.motion.velocity.z = 0.0;
    }

    const QcVec3 flat{mob.motion.velocity.x, 0.0, mob.motion.velocity.z};
    if (flat.length() > 0.05) {
        mob.yaw = QcMobSteering::yawTowards(flat);
    }
}

void QcFishSwimming::flopOnLand(QcCreature &mob, const QcMobDefinition &definition, const QcBlockAccess &world, double dt)
{
    const QcAquaticDefinition &aquatic = *definition.aquatic; // NOLINT(bugprone-unchecked-optional-access): swim() checked it
    ++mob.dryTicks;

    // On the ground now and then a flop in a random direction, which may bring it back to the water.
    const bool flops = mob.motion.onGround && !mob.hurt.knockedBack() && m_random.bounded(1.0) < flopChance;
    if (flops) {
        const double angle = m_random.bounded(2.0 * std::numbers::pi);
        const double sideways = aquatic.flopSpeed / 3.0;
        mob.motion.velocity = {std::sin(angle) * sideways, aquatic.flopSpeed, -std::cos(angle) * sideways};
        mob.yaw = static_cast<float>(m_random.bounded(360.0));
    }

    // It keeps the speed of its flop through the air (a body without wish keeps nothing on the ground).
    const QcVec3 flat{mob.motion.velocity.x, 0.0, mob.motion.velocity.z};
    const double speed = flat.length();
    mob.motion.step(QcPhysics::Steering{.wish = speed > 1e-6 ? flat * (1.0 / speed) : QcVec3{}, .speed = mob.motion.onGround && !flops ? 0.0 : speed},
                          QcPhysics::BodySize{definition.width, definition.height}, world, dt);
}

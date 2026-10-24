#include "clientcore/debug/QcDebugLines.h"

#include "clientcore/session/QcClientSession.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcSectionKey.h"
#include "common/core/QcWorldConstants.h"
#include "common/physics/QcPose.h"
#include "common/world/QcMinecartBody.h"

#include <array>
#include <cmath>
#include <numbers>

namespace {

QcVec3 lerp(const QcVec3 &a, const QcVec3 &b, double t)
{
    return a + (b - a) * t;
}

// Width and height of what an entity's hitbox covers.
std::pair<double, double> bodySize(const QcClientEntity &entity, const QcContentRegistry &content)
{
    if (entity.isItem()) {
        return {0.25, 0.25};
    }
    if (entity.isBlockEntity()) {
        return {0.98, 0.98};
    }
    if (entity.isMinecart()) {
        return {QcMinecartBody::width, QcMinecartBody::height};
    }
    if (const QcMobDefinition *creature = entity.creature(content)) {
        return {creature->width, creature->height};
    }
    return {0.6, 1.8};
}

} // namespace

void QcDebugLines::addLine(const QcVec3 &from, const QcVec3 &to, QRgb color)
{
    for (const QcVec3 &end : {from, to}) {
        const QcVec3 local = end - m_origin;
        QcDebugLineVertex vertex;
        vertex.x = static_cast<float>(local.x);
        vertex.y = static_cast<float>(local.y);
        vertex.z = static_cast<float>(local.z);
        vertex.rgba[0] = static_cast<quint8>(qRed(color));
        vertex.rgba[1] = static_cast<quint8>(qGreen(color));
        vertex.rgba[2] = static_cast<quint8>(qBlue(color));
        vertex.rgba[3] = static_cast<quint8>(qAlpha(color));
        m_vertices.append(vertex);
    }
}

void QcDebugLines::addBox(const QcAabb &box, QRgb color)
{
    const QcVec3 &a = box.min;
    const QcVec3 &b = box.max;
    const std::array<QcVec3, 8> c{{{a.x, a.y, a.z}, {b.x, a.y, a.z}, {b.x, a.y, b.z}, {a.x, a.y, b.z},
                                   {a.x, b.y, a.z}, {b.x, b.y, a.z}, {b.x, b.y, b.z}, {a.x, b.y, b.z}}};
    constexpr std::array<std::pair<int, int>, 12> edges{
        {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};
    for (const auto &[from, to] : edges) {
        addLine(c[static_cast<std::size_t>(from)], c[static_cast<std::size_t>(to)], color);
    }
}

void QcDebugLines::addChunkBorders(const QcVec3 &feet)
{
    constexpr double size = QcWorld::chunkSize;
    constexpr double top = QcWorld::worldHeight;
    const QcChunkPos chunk = QcBlockPos{static_cast<int>(std::floor(feet.x)), 0, static_cast<int>(std::floor(feet.z))}.chunk();

    for (int dx = -chunkCornerRadius; dx <= chunkCornerRadius + 1; ++dx) {
        for (int dz = -chunkCornerRadius; dz <= chunkCornerRadius + 1; ++dz) {
            const double x = (chunk.x + dx) * size;
            const double z = (chunk.z + dz) * size;
            const bool own = dx >= 0 && dx <= 1 && dz >= 0 && dz <= 1;
            addLine({x, 0.0, z}, {x, top, z}, own ? sectionColor : chunkCornerColor);
        }
    }

    const double x0 = chunk.x * size;
    const double z0 = chunk.z * size;
    for (int section = 0; section <= QcWorld::sectionCount; ++section) {
        const double y = section * QcWorld::sectionHeight;
        addBox({{x0, y, z0}, {x0 + size, y, z0 + size}}, sectionColor);
    }

    // A grid on the four walls around the player's height: every four blocks across and up.
    const double low = std::floor(feet.y / 4.0) * 4.0 - 8.0;
    for (int block = 4; block < QcWorld::chunkSize; block += 4) {
        const double step = block;
        addLine({x0 + step, low, z0}, {x0 + step, low + 16.0, z0}, chunkGridColor);
        addLine({x0 + step, low, z0 + size}, {x0 + step, low + 16.0, z0 + size}, chunkGridColor);
        addLine({x0, low, z0 + step}, {x0, low + 16.0, z0 + step}, chunkGridColor);
        addLine({x0 + size, low, z0 + step}, {x0 + size, low + 16.0, z0 + step}, chunkGridColor);
    }
    for (int row = 0; row <= 4; ++row) {
        const double y = low + row * 4.0;
        addBox({{x0, y, z0}, {x0 + size, y, z0 + size}}, chunkGridColor);
    }
}

void QcDebugLines::addHitboxes(const QcClientSession &session, double alpha)
{
    const std::shared_ptr<const QcContentRegistry> content = session.content();
    if (!content) {
        return;
    }

    for (const QcClientEntity &entity : session.entities()) {
        const QcVec3 at = lerp(entity.previousPosition, entity.position, alpha);
        const auto [width, height] = bodySize(entity, *content);
        addBox(QcPoses::boxAt(QcMsg::poseOf(entity.flags), at, entity.yaw, width, height), hitboxColor);

        if (!entity.isItem() && !entity.isBlockEntity()) {
            // Yaw 0 looks north (-Z), 90 east (+X).
            const double yaw = static_cast<double>(entity.yaw) * std::numbers::pi / 180.0;
            const QcVec3 eye{at.x, at.y + height * 0.85, at.z};
            addLine(eye, eye + QcVec3{std::sin(yaw), 0.0, -std::cos(yaw)} * 1.5, lookColor);
        }
    }
}

void QcDebugLines::addMobGoals(const QcClientSession &session, const QList<QcMsg::MobGoal> &goals, double alpha)
{
    for (const QcMsg::MobGoal &goal : goals) {
        const auto found = session.entities().constFind(goal.entityId);
        if (found == session.entities().constEnd()) {
            continue;
        }

        const QcVec3 from = lerp(found->previousPosition, found->position, alpha) + QcVec3{0.0, 0.5, 0.0};
        const QRgb color = goal.kind == QcMsg::MobGoal::Kind::Chase  ? chaseColor
                           : goal.kind == QcMsg::MobGoal::Kind::Flee ? fleeColor
                                                                     : wanderColor;
        const QcVec3 to = goal.goal + QcVec3{0.0, 0.5, 0.0};
        addLine(from, to, color);
        addBox({to - QcVec3{0.15, 0.15, 0.15}, to + QcVec3{0.15, 0.15, 0.15}}, color);
    }
}

void QcDebugLines::addRemeshFlashes(const QHash<quint64, qint64> &meshedAt, qint64 nowMs)
{
    for (auto it = meshedAt.constBegin(); it != meshedAt.constEnd(); ++it) {
        const qint64 age = nowMs - it.value();
        if (age < 0 || age >= flashMs) {
            continue;
        }
        const int alpha = static_cast<int>(255 * (flashMs - age) / flashMs);
        addBox(sectionBox(it.key()), qRgba(qRed(flashColor), qGreen(flashColor), qBlue(flashColor), alpha));
    }
}

QcAabb QcDebugLines::sectionBox(quint64 sectionKey)
{
    const QcSectionAddress section = qcSectionFromKey(sectionKey);
    const double x = section.chunk.x * static_cast<double>(QcWorld::chunkSize);
    const double z = section.chunk.z * static_cast<double>(QcWorld::chunkSize);
    const double y = section.sectionIndex * static_cast<double>(QcWorld::sectionHeight);
    // A little inside the section, so its box does not hide in the faces of its neighbours.
    constexpr double inset = 0.02;
    return {{x + inset, y + inset, z + inset},
            {x + QcWorld::chunkSize - inset, y + QcWorld::sectionHeight - inset, z + QcWorld::chunkSize - inset}};
}

#include "clientcore/light/QcMovingLights.h"

#include "clientcore/session/QcClientSession.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/physics/QcPhysics.h"

#include <QHash>

#include <algorithm>
#include <cmath>
#include <optional>
#include <tuple>

namespace {

QcVec3 between(const QcVec3 &a, const QcVec3 &b, double t)
{
    return a + (b - a) * t;
}

// The light levels of items, looked up once per item in one gathering.
class ItemLevels
{
public:
    explicit ItemLevels(const QcContentRegistry &content)
        : m_content(content)
    {
    }

    float level(QcItemId item)
    {
        const auto known = m_levels.constFind(item);
        if (known != m_levels.constEnd()) {
            return *known;
        }
        return *m_levels.insert(item, QcMovingLights::itemLevel(m_content, item));
    }

    // The warm light of `item` at `position`, if the item gives light.
    std::optional<QcMovingLight> light(QcItemId item, const QcVec3 &position)
    {
        const float found = level(item);
        return found > 0.0f ? std::optional(QcMovingLight{position, found, QcMovingLight::Colour::Warm}) : std::nullopt;
    }

    const QcContentRegistry &content() const { return m_content; }

private:
    const QcContentRegistry &m_content;
    QHash<QcItemId, float> m_levels;
};

// The lights an entity of the mirror brings: what it holds, its glow, a glowing item it is.
QList<QcMovingLight> lightsOf(ItemLevels &items, const QcClientEntity &entity, double alpha)
{
    QList<QcMovingLight> lights;
    const QcVec3 feet = between(entity.previousPosition, entity.position, alpha);
    const auto add = [&lights](std::optional<QcMovingLight> light) {
        if (light) {
            lights.append(*light);
        }
    };
    if (entity.isItem()) {
        add(items.light(entity.item.item, feet + QcVec3{0.0, QcMovingLights::itemCentre, 0.0}));
    } else if (entity.isPlayer()) {
        add(items.light(entity.held, feet + QcVec3{0.0, QcPhysics::playerEyeHeight - QcMovingLights::heldBelowEyes, 0.0}));
    } else if (const QcMobDefinition *mob = entity.creature(items.content())) {
        add(items.light(entity.held, feet + QcVec3{0.0, mob->height * QcMovingLights::creatureHeldHeight, 0.0}));
        const float level = QcMovingLights::creatureLevel(*mob);
        if (level > 0.0f) {
            lights.append({feet + QcVec3{0.0, mob->height * QcMovingLights::creatureGlowHeight, 0.0}, level, QcMovingLight::Colour::Glow});
        }
    }
    return lights;
}

} // namespace

float QcMovingLights::itemLevel(const QcContentRegistry &content, QcItemId item)
{
    if (!content.isValidItem(item)) {
        return 0.0f;
    }
    const QString &places = content.item(item).placesBlock;
    const std::optional<QcBlockId> block = places.isEmpty() ? std::nullopt : content.blockId(places);
    return block ? static_cast<float>(std::clamp(content.block(*block).lightLevel, 0, QcBlockDefinition::maxLightLevel)) : 0.0f;
}

float QcMovingLights::creatureLevel(const QcMobDefinition &mob)
{
    return static_cast<float>(std::clamp(mob.glow, 0, QcBlockDefinition::maxLightLevel)) * creatureGlowShare;
}

float QcMovingLights::brightness(float level, double distance)
{
    // NaN (a broken position) gives no light rather than full light.
    const double share = (static_cast<double>(level) - distance) / static_cast<double>(QcBlockDefinition::maxLightLevel);
    return std::isfinite(share) ? static_cast<float>(std::clamp(share, 0.0, 1.0)) : 0.0f;
}

QList<QcMovingLight> QcMovingLights::select(const QList<QcMovingLight> &candidates, const QcVec3 &camera, int count)
{
    struct Ranked
    {
        double score = 0.0;
        QcMovingLight light;
    };
    QList<Ranked> ranked;
    ranked.reserve(candidates.size());
    for (const QcMovingLight &light : candidates) {
        const double score = static_cast<double>(light.level) - (light.position - camera).length();
        if (light.level > 0.0f && std::isfinite(score) && score > -farthest) {
            ranked.append({score, light});
        }
    }
    const auto order = [](const Ranked &a, const Ranked &b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        const QcVec3 &p = a.light.position;
        const QcVec3 &q = b.light.position;
        return std::tie(p.x, p.y, p.z, a.light.level) < std::tie(q.x, q.y, q.z, b.light.level);
    };
    std::ranges::sort(ranked, order);

    QList<QcMovingLight> chosen;
    const qsizetype kept = std::min(ranked.size(), static_cast<qsizetype>(std::max(count, 0)));
    chosen.reserve(kept);
    for (qsizetype i = 0; i < kept; ++i) {
        chosen.append(ranked[i].light);
    }
    return chosen;
}

QcPackedLights QcMovingLights::pack(const QList<QcMovingLight> &lights, const QcVec3 &camera, int most)
{
    QcPackedLights packed;
    const int limit = std::clamp(most, 0, QcRenderOptions::maxMovingLights);
    for (const QcMovingLight &light : lights) {
        if (packed.count >= limit) {
            break;
        }
        const QcVec3 relative = light.position - camera;
        if (!std::isfinite(relative.dot(relative)) || !std::isfinite(light.level) || light.level <= 0.0f) {
            continue;
        }
        // Below 16, so the colour stays apart from the level.
        const float level = std::min(light.level, 15.99f);
        const float colour = 16.0f * static_cast<float>(light.colour);
        packed.lights[static_cast<std::size_t>(packed.count)] = {static_cast<float>(relative.x), static_cast<float>(relative.y),
                                                                 static_cast<float>(relative.z), level + colour};
        ++packed.count;
    }
    return packed;
}

bool QcMovingLights::handInWorld(bool dead, QcGameModes::Rules rules)
{
    // A spectator holds nothing in the world (decision E-730).
    return !dead && rules.tangible;
}

std::optional<QcMovingLight> QcMovingLights::handLight(const QcContentRegistry &content, const QcItemStack &held, const QcVec3 &eye)
{
    const float level = held.isEmpty() ? 0.0f : itemLevel(content, held.item);
    if (level <= 0.0f) {
        return std::nullopt;
    }
    return QcMovingLight{eye - QcVec3{0.0, heldBelowEyes, 0.0}, level, QcMovingLight::Colour::Warm};
}

QList<QcMovingLight> QcMovingLights::entityLights(const QcContentRegistry &content, const QHash<quint32, QcClientEntity> &entities,
                                                  double alpha)
{
    ItemLevels items(content);
    QList<QcMovingLight> lights;
    for (const QcClientEntity &entity : entities) {
        lights.append(lightsOf(items, entity, alpha));
    }
    return lights;
}

QList<QcMovingLight> QcMovingLights::gather(const QcClientSession &session, double alpha, const QcVec3 &eye)
{
    const std::shared_ptr<const QcContentRegistry> content = session.content();
    if (!content) {
        return {};
    }

    QList<QcMovingLight> lights = entityLights(*content, session.entities(), alpha);
    if (handInWorld(session.isDead(), session.gameMode().rules())) {
        if (const std::optional<QcMovingLight> light = handLight(*content, session.inventory().selectedStack(), eye)) {
            lights.prepend(*light);
        }
    }
    return lights;
}

#include "clientcore/meshing/QcTethers.h"

#include "clientcore/session/QcClientEntity.h"
#include "common/content/registry/QcContentRegistry.h"

#include <algorithm>
#include <utility>

namespace {

// The middle of a creature's body at `alpha`.
QcVec3 middleOf(const QcClientEntity &entity, const QcMobDefinition &kind, double alpha)
{
    return entity.previousPosition + (entity.position - entity.previousPosition) * alpha + QcVec3{0.0, kind.height * 0.5, 0.0};
}

} // namespace

void QcTethers::learn(const std::shared_ptr<const QcContentRegistry> &content)
{
    if (content == m_content) {
        return;
    }
    m_content = content;
    m_parentOf.clear();
    m_matches.clear();
    if (!m_content) {
        return;
    }
    for (qsizetype type = 0; type < m_content->mobs().size(); ++type) {
        const std::optional<QcFamilyDefinition> &family = m_content->mobs()[type].family;
        if (family && family->childType >= 0 && !family->tetherTexture.isEmpty()) {
            m_parentOf.insert(family->childType, {static_cast<int>(type), &*family});
        }
    }
}

const QcClientEntity *QcTethers::parentOf(const QcClientEntity &child, Parent kind, const QList<const QcClientEntity *> &parents,
                                          double alpha) const
{
    // The nearest parent in range, and the one of the last frame if it is still in range.
    const QcMobDefinition &parentKind = m_content->mobs()[kind.type];
    const QcVec3 to = middleOf(child, m_content->mobs()[child.creatureType], alpha);
    const quint32 previous = m_matches.value(child.id);
    const QcClientEntity *nearest = nullptr;
    const QcClientEntity *kept = nullptr;
    double nearestDistance = kind.family->tetherRange;
    double keptDistance = 0.0;
    for (const QcClientEntity *parent : parents) {
        const double distance = (middleOf(*parent, parentKind, alpha) - to).length();
        if (parent->creatureType != kind.type || distance > kind.family->tetherRange) {
            continue;
        }
        if (parent->id == previous) {
            kept = parent;
            keptDistance = distance;
        }
        if (distance <= nearestDistance) {
            nearestDistance = distance;
            nearest = parent;
        }
    }

    // The last one stays unless the nearest is clearly nearer.
    return kept && nearestDistance >= keptDistance * switchShare ? kept : nearest;
}

QList<QcTetherMesher::Thread> QcTethers::threads(const QHash<quint32, QcClientEntity> &entities,
                                                 const std::shared_ptr<const QcContentRegistry> &content, double alpha)
{
    learn(content);
    if (m_parentOf.isEmpty()) {
        m_matches.clear();
        return {};
    }

    QList<const QcClientEntity *> parents;
    for (const QcClientEntity &entity : entities) {
        if (entity.isCreature() && std::ranges::any_of(m_parentOf, [&entity](Parent parent) { return parent.type == entity.creatureType; })) {
            parents.append(&entity);
        }
    }
    QList<QcTetherMesher::Thread> result;
    QHash<quint32, quint32> matches;
    for (const QcClientEntity &child : entities) {
        const Parent kind = child.isCreature() ? m_parentOf.value(child.creatureType) : Parent{};
        const QcClientEntity *parent = kind.family ? parentOf(child, kind, parents, alpha) : nullptr;
        if (!parent) {
            continue;
        }
        matches.insert(child.id, parent->id);
        result.append({middleOf(*parent, m_content->mobs()[kind.type], alpha), middleOf(child, m_content->mobs()[child.creatureType], alpha),
                       kind.family->tetherTexture});
    }
    m_matches = std::move(matches);
    return result;
}

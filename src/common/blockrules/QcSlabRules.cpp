#include "common/blockrules/QcSlabRules.h"

#include "common/content/registry/QcContentRegistry.h"

using namespace Qt::StringLiterals;

namespace QcSlabRules {

[[nodiscard]] std::optional<QcBlockRules::Placement> doubled(const QcBlockAccess &world, QcBlockId state, QcBlockPos clicked, QcBlockFace face,
                                                             bool upper)
{
    const QcContentRegistry &content = world.content();
    const std::optional<QcBlockId> slabBelow = world.blockAt(clicked);
    if (slabBelow && content.isSameBlock(*slabBelow, state)) {
        const QString type = content.property(*slabBelow, u"type"_s);
        if ((type == u"bottom"_s && face == QcBlockFace::Up) || (type == u"top"_s && face == QcBlockFace::Down)) {
            return QcBlockRules::Placement{clicked, content.withProperty(state, u"type"_s, u"double"_s)};
        }
    }

    const QcBlockPos target = qcNeighbour(clicked, face);
    const std::optional<QcBlockId> existing = world.blockAt(target);
    if (existing && content.isSameBlock(*existing, state)) {
        const QString type = content.property(*existing, u"type"_s);
        if ((type == u"bottom"_s && upper) || (type == u"top"_s && !upper)) {
            return QcBlockRules::Placement{target, content.withProperty(state, u"type"_s, u"double"_s)};
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<QcBlockRules::Placement> doubledVertical(const QcBlockAccess &world, QcBlockId state, QcBlockPos clicked,
                                                                     QcBlockFace face, const QString &facing)
{
    const QcContentRegistry &content = world.content();
    // The single vertical slab of this block at `pos` facing `side`, if there is one.
    const auto singleAt = [&](QcBlockPos pos, const QString &side) -> std::optional<QcBlockId> {
        const std::optional<QcBlockId> block = world.blockAt(pos);
        if (!block || !content.isSameBlock(*block, state) || content.property(*block, u"double"_s) != u"false"_s
            || content.property(*block, u"facing"_s) != side) {
            return std::nullopt;
        }
        return block;
    };

    // Clicked on its open side: the face looking away from the side it stands against.
    if (const std::optional<QcBlockId> open = singleAt(clicked, QcBlockRules::faceName(QcBlockRules::opposite(face)))) {
        return QcBlockRules::Placement{clicked, content.withProperty(*open, u"double"_s, u"true"_s)};
    }

    // Placed into a cell whose vertical slab faces the other way.
    const std::optional<QcBlockFace> towards = QcBlockRules::faceFromName(facing);
    if (!towards) {
        return std::nullopt;
    }
    const QcBlockPos target = qcNeighbour(clicked, face);
    if (const std::optional<QcBlockId> facingBack = singleAt(target, QcBlockRules::faceName(QcBlockRules::opposite(*towards)))) {
        return QcBlockRules::Placement{target, content.withProperty(*facingBack, u"double"_s, u"true"_s)};
    }
    return std::nullopt;
}

bool isVertical(const QcBlockDefinition &definition)
{
    const QcBlockProperty *both = definition.property(u"double"_s);
    return definition.facingRule == QcFacingRule::Against && both && both->values.contains(u"true"_s);
}

} // namespace QcSlabRules

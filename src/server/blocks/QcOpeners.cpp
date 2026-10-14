#include "QcOpeners.h"

#include "QcBlockRules.h"
#include "QcServerBlocks.h"
#include "QcServerSounds.h"
#include "QcServerState.h"

#include <utility>

using namespace Qt::StringLiterals;

QcOpeners::QcOpeners(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks)
    : m_state(state)
    , m_sounds(sounds)
    , m_blocks(blocks)
{
}

bool QcOpeners::toggle(QcBlockPos pos, QcBlockId block)
{
    const QcContentRegistry &content = m_state.content();
    const QString open = content.property(block, u"open"_s) == u"true"_s ? u"false"_s : u"true"_s;
    const QcBlockDefinition &definition = content.block(block);
    QList<std::pair<QcBlockPos, QcBlockId>> changes{{pos, content.withProperty(block, u"open"_s, open)}};
    if (definition.door) {
        const QcBlockPos other = QcBlockRules::otherDoorHalf(m_state.world(), pos, block);
        const std::optional<QcBlockId> partner = m_state.world().blockAt(other);
        if (partner && content.isSameBlock(*partner, block)) {
            changes.append({other, content.withProperty(*partner, u"open"_s, open)});
        }
    }

    // Swinging shut (or open) must not trap anyone inside the new shape.
    QList<QcAabb> boxes;
    for (const auto &[at, state] : std::as_const(changes)) {
        const QcVec3 corner{double(at.x), double(at.y), double(at.z)};
        for (const QcAabb &box : content.state(state).collision) {
            boxes.append(box.moved(corner));
        }
    }

    if (m_state.entities().overlapsLiving(boxes)) {
        return false;
    }

    for (const auto &[at, state] : std::as_const(changes)) {
        m_blocks.setAndBroadcast(at, state);
    }

    m_sounds.playBlock(definition, QcBlockSoundAction::Place, pos);
    // Changed only when the world took the new state (it refuses, for example, outside loaded chunks).
    return m_state.world().blockAt(pos) != block;
}

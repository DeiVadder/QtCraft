#include "worldgen/villages/QcVillageBlocks.h"

using namespace Qt::StringLiterals;

QcVillageBlocks::QcVillageBlocks(const QcContentRegistry &content, QStringList *warnings)
    : m_bedHead(std::size_t(content.stateCount()))
    , m_bell(std::size_t(content.stateCount()))
    , m_kind(std::size_t(content.stateCount()))
{
    const QcVillageSettings &settings = content.villageSettings();
    for (auto it = settings.workstations.constBegin(); it != settings.workstations.constEnd(); ++it) {
        if (!content.blockId(it.key()) && warnings) {
            warnings->append(u"village settings: unknown workstation block '%1'"_s.arg(it.key()));
        }
    }

    for (std::size_t id = 0; id < m_kind.size(); ++id) {
        const QcBlockDefinition &block = content.block(QcBlockId(id));
        m_bedHead[id] = block.bed && content.property(QcBlockId(id), u"part"_s) == u"head"_s;
        m_bell[id] = !settings.bell.isEmpty() && block.name == settings.bell;
        m_kind[id] = settings.workstations.value(block.name);
    }
}

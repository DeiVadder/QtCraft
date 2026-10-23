#include "server/gameplay/QcToolWear.h"

#include "common/gameplay/QcEnchanting.h"
#include "server/core/QcServerState.h"

namespace QcToolWear {

QcItemStack worn(QcServerState &state, const QcItemStack &tool)
{
    const QcContentRegistry &content = state.content();
    if (tool.isEmpty() || !content.isValidItem(tool.item) || content.item(tool.item).durability <= 0) {
        return tool;
    }
    const int unbreaking = QcEnchanting::levelOf(content, tool, QcEnchantmentEffect::Unbreaking);
    if (unbreaking > 0 && QcEnchanting::keepsDurability(unbreaking, state.random().generateDouble(), false)) {
        return tool;
    }
    QcItemStack next = tool;
    if (++next.damage >= content.item(tool.item).durability) {
        return {};
    }
    return next;
}

bool armourKeeps(QcServerState &state, const QcItemStack &stack)
{
    const int unbreaking = QcEnchanting::levelOf(state.content(), stack, QcEnchantmentEffect::Unbreaking);
    return unbreaking > 0 && QcEnchanting::keepsDurability(unbreaking, state.random().generateDouble(), true);
}

} // namespace QcToolWear

#include "server/gameplay/QcSpectatorRules.h"

#include "common/core/QcWorldConstants.h"
#include "common/physics/QcPhysics.h"
#include "server/core/QcServerClient.h"

#include <cmath>

namespace QcSpectatorRules {

namespace {

// The requests of a player who acts on the world or the inventory.
bool acts(const QcMessage &message)
{
    if (const auto *draw = std::get_if<QcMsg::DrawLauncher>(&message)) {
        return draw->action == QcMsg::DrawLauncher::Action::Draw;
    }
    return std::holds_alternative<QcMsg::StartMining>(message) || std::holds_alternative<QcMsg::FinishMining>(message)
           || std::holds_alternative<QcMsg::UseItem>(message) || std::holds_alternative<QcMsg::UseEntity>(message)
           || std::holds_alternative<QcMsg::AttackEntity>(message) || std::holds_alternative<QcMsg::ConsumeItem>(message)
           || std::holds_alternative<QcMsg::ThrowItem>(message) || std::holds_alternative<QcMsg::MoveInventoryItem>(message)
           || std::holds_alternative<QcMsg::QuickMoveItem>(message) || std::holds_alternative<QcMsg::TakeCraftingResult>(message)
           || std::holds_alternative<QcMsg::CraftRecipe>(message) || std::holds_alternative<QcMsg::Trade>(message)
           || std::holds_alternative<QcMsg::DropItem>(message) || std::holds_alternative<QcMsg::CreativeItem>(message)
           || std::holds_alternative<QcMsg::PickBlock>(message) || std::holds_alternative<QcMsg::EditSign>(message)
           || std::holds_alternative<QcMsg::SelectStonecutterRecipe>(message) || std::holds_alternative<QcMsg::ChooseEnchantment>(message)
           || std::holds_alternative<QcMsg::RenameAnvil>(message) || std::holds_alternative<QcMsg::ForgeAnvil>(message);
}

} // namespace

bool allows(const QcServerClient &client, const QcMessage &message)
{
    return client.rules().interacts || !acts(message);
}

std::optional<QcVec3> freePlaceAbove(const QcBlockAccess &world, const QcVec3 &feet)
{
    const auto freeAt = [&world](const QcVec3 &at) {
        return !QcPhysics::overlapsBlocks(world, QcPhysics::boxAt(at, QcPhysics::playerWidth, QcPhysics::playerHeight));
    };
    if (freeAt(feet)) {
        return feet;
    }

    // Whole cells upwards from the one above the feet, so the body stands on what it rose out of.
    for (int y = static_cast<int>(std::floor(feet.y)) + 1; y < QcWorld::worldHeight; ++y) {
        const QcVec3 candidate{feet.x, static_cast<double>(y), feet.z};
        if (freeAt(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

} // namespace QcSpectatorRules

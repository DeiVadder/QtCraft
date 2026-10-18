#include "server/spawners/QcFurnishing.h"

#include "server/core/QcServerState.h"

#include <QLoggingCategory>

namespace {

Q_LOGGING_CATEGORY(lcFurnishing, "qtcraft.server")

// Fills the chest at `chest.at` with its loot table's draw; a chest that holds something already
// (a save from before the mark) is left as it is.
void fillChest(QcServerState &state, const QcChunkFurnishing::Chest &chest)
{
    QcBlockEntities::Container *container = state.blockEntities().container(chest.at);
    const QcLootTableDefinition *table = state.content().lootTables().find(chest.lootTable);
    if (!table) {
        qCWarning(lcFurnishing) << "Unknown loot table" << chest.lootTable << "for the chest at" << chest.at.x << chest.at.y << chest.at.z;
        return;
    }
    if (!container || std::ranges::any_of(container->stacks, [](QcItemStack stack) { return !stack.isEmpty(); })) {
        return;
    }

    const QcContentRegistry &content = state.content();
    const auto items = [&content](const QString &name) -> std::optional<QcLootTables::ItemInfo> {
        const std::optional<QcItemId> id = content.itemId(name);
        if (!id) {
            return std::nullopt;
        }
        return QcLootTables::ItemInfo{*id, content.item(*id).maxStack};
    };
    const QList<QcItemStack> stacks = QcLootTables::draw(*table, chest.seed, items);
    container->stacks = QcLootTables::scatter(stacks, int(container->stacks.size()), chest.seed);
}

} // namespace

namespace QcFurnishing {

void furnish(QcServerState &state, QcChunkPos pos)
{
    QcServerWorld &world = state.world();
    if (world.isFurnished(pos) || !world.chunk(pos)) {
        return;
    }
    // Without a spawner or a block with storage there is nothing to put in; most chunks end here,
    // before the generator is asked (this runs on the tick thread).
    const QcSectionMarks &marks = world.marks();
    if (marks.sectionsOf(pos, QcBlockTrait::Spawner) == 0 && marks.sectionsOf(pos, QcBlockTrait::BlockEntity) == 0) {
        return;
    }
    const QcChunkFurnishing furnishing = world.generator().furnishingIn(pos);
    if (furnishing.isEmpty()) {
        return;
    }

    for (const QcChunkFurnishing::Chest &chest : furnishing.chests) {
        if (chest.at.chunk() == pos) {
            fillChest(state, chest);
        }
    }
    for (const QcChunkFurnishing::Spawner &spawner : furnishing.spawners) {
        if (spawner.at.chunk() == pos) {
            world.spawners().set({spawner.at, spawner.mob, QcSpawners::firstDelay, spawner.confined});
        }
    }
    world.noteFurnished(pos);
}

} // namespace QcFurnishing

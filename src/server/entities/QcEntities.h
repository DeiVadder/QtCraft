#pragma once

#include "common/core/QcAabb.h"
#include "common/physics/QcPhysics.h"
#include "server/blocks/QcFallingBlockManager.h"
#include "server/core/QcServerClient.h"
#include "server/explosions/QcPrimedExplosives.h"
#include "server/gameplay/QcArmourStandManager.h"
#include "server/hanging/QcHangingManager.h"
#include "server/items/QcExperienceOrbManager.h"
#include "server/items/QcItemEntityManager.h"
#include "server/minecarts/QcMinecartManager.h"
#include "server/mobs/QcMobManager.h"
#include "server/projectiles/QcProjectileManager.h"
#include "server/signal/QcMovingBlocks.h"

#include <QList>
#include <QSet>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

// The entity register of a world (decision E-364): every entity is a QcEntity of one kind and lives
// in the table of its kind, which also carries the kind's own behaviour (QcMobManager: creature AI
// and spawning; QcItemEntityManager: items falling, merging and ageing; QcFallingBlockManager;
// QcArmourStandManager; QcMovingBlocks; QcMinecartManager: carts on rails; QcExperienceOrbManager; QcProjectileManager: arrows; QcPrimedExplosives: lit explosives). Players are the clients of the server and are only visited
// here. What exists once for all kinds lives in this class: ids, visiting every entity in a fixed
// order, the chunk an entity belongs to and whether it rests there, the box of a
// body, moving a body from outside (pistons), knowing which clients must hear of an entity again.
class QcEntities
{
public:
    using Clients = std::vector<std::unique_ptr<QcServerClient>>;

    using ChunkLoadedFunction = std::function<bool(QcChunkPos)>;

    QcEntities(const std::shared_ptr<const QcContentRegistry> &content, quint64 seed, const Clients &clients);

    // Hands the entity limits of the config to the tables of the kinds (decision E-349).
    void applyLimits(const QcServerLimits &limits);

    // A new id for any entity, players included; never given twice while the world is open.
    quint32 newId() { return m_nextId++; }

    QcMobManager &creatures() { return m_creatures; }
    const QcMobManager &creatures() const { return m_creatures; }
    QcItemEntityManager &items() { return m_items; }
    const QcItemEntityManager &items() const { return m_items; }
    QcFallingBlockManager &fallingBlocks() { return m_falling; }
    const QcFallingBlockManager &fallingBlocks() const { return m_falling; }
    QcArmourStandManager &stands() { return m_stands; }
    const QcArmourStandManager &stands() const { return m_stands; }
    QcMovingBlocks &movingBlocks() { return m_moving; }
    const QcMovingBlocks &movingBlocks() const { return m_moving; }
    QcHangingManager &hangings() { return m_hangings; }
    const QcHangingManager &hangings() const { return m_hangings; }
    QcMinecartManager &minecarts() { return m_carts; }
    const QcMinecartManager &minecarts() const { return m_carts; }
    QcExperienceOrbManager &orbs() { return m_orbs; }
    const QcExperienceOrbManager &orbs() const { return m_orbs; }
    QcProjectileManager &projectiles() { return m_projectiles; }
    const QcProjectileManager &projectiles() const { return m_projectiles; }
    QcPrimedExplosives &explosives() { return m_explosives; }
    const QcPrimedExplosives &explosives() const { return m_explosives; }
    // The clients, whose players are entities too (only playing, living ones are visited).
    const Clients &clients() const { return m_clients; }

    // Every entity in a fixed order: creatures, players (those playing and alive), falling blocks,
    // sliding blocks, items, armour stands, minecarts, experience orbs, projectiles, lit explosives, frames and paintings; each kind in the order of its table. `visit` takes a
    // QcEntity and must not add or remove entities.
    template<typename Visit>
    void forEach(Visit &&visit)
    {
        visitAll(*this, visit);
    }
    template<typename Visit>
    void forEach(Visit &&visit) const
    {
        visitAll(*this, visit);
    }
    // The same for the bodies only: every kind but sliding blocks, which only show a move.
    template<typename Visit>
    void forEachBody(Visit &&visit)
    {
        visitAll(*this, [&visit](auto &entity) {
            if (entity.kind != QcEntityKind::MovingBlock) {
                visit(entity);
            }
        });
    }
    template<typename Visit>
    void forEachBody(Visit &&visit) const
    {
        visitAll(*this, [&visit](const auto &entity) {
            if (entity.kind != QcEntityKind::MovingBlock) {
                visit(entity);
            }
        });
    }

    // The chunk an entity belongs to: the one holding its feet.
    static QcChunkPos chunkOf(const QcEntity &entity) { return QcPhysics::blockContaining(entity.motion.position).chunk(); }
    // Rests every entity whose chunk is not loaded and wakes the others; runs at the start of every
    // tick after the chunks loaded, so all kinds rest by the same rule. Players never rest, nor do
    // sliding blocks: a piston moves them only within loaded chunks and they last three ticks.
    // `loadedEpoch` changes whenever a chunk enters or leaves the world (QcServerWorld::loadedEpoch):
    // while it stays, only entities that are new or changed their chunk are looked up. Returns the
    // chunks of the entities looked up that rest (strays: in a chunk that is not loaded, decision E-374).
    QSet<QcChunkPos> settle(const ChunkLoadedFunction &isLoaded, quint64 loadedEpoch);
    // The same for the entities of one table (tests of a kind's behaviour on its own).
    template<typename List>
    static void settle(List &entities, const ChunkLoadedFunction &isLoaded)
    {
        for (QcEntity &entity : entities) {
            entity.dormant = !isLoaded(chunkOf(entity));
        }
    }

    // Where the body of an entity is in the world; nothing for sliding blocks and for creatures of a
    // kind the content does not know.
    [[nodiscard]] std::optional<QcAabb> boxOf(const QcEntity &entity) const;
    // Moves a body by `delta` from outside its own motion (a piston pushed it); its kind takes note:
    // the player gets his new position, an item stops resting, a stand is sent again. A player who
    // rides a minecart stays in it: only the cart moves (decision E-382).
    void displace(QcEntity &entity, const QcVec3 &delta) const;
    // Whether any of `boxes` (world space) overlaps a living body: a playing player or a creature.
    bool overlapsLiving(const QList<QcAabb> &boxes) const;
    // Clients that know the entity get its spawn again (a new look, armour, pose or place).
    void markStale(quint32 id) const;
    // Entities that died in this tick: their removal tells clients to play the death animation.
    void noteDeath(quint32 id) { m_died.insert(id); }
    bool diedThisTick(quint32 id) const { return m_died.contains(id); }
    void forgetDeaths() { m_died.clear(); }

private:
    // Spectators have no body (decision E-730): no kind of the register sees, meets or syncs them.
    static bool isPresent(const QcServerClient &client) { return client.hasBody(); }

    template<typename Self, typename Visit>
    static void visitAll(Self &self, Visit &&visit)
    {
        constexpr bool readOnly = std::is_const_v<Self>;
        const auto each = [&visit](auto &list) {
            for (auto &entity : list) {
                visit(entity);
            }
        };
        if constexpr (readOnly) {
            each(self.m_creatures.mobs());
        } else {
            each(self.m_creatures.editableMobs());
        }
        for (const std::unique_ptr<QcServerClient> &client : self.m_clients) {
            if (isPresent(*client)) {
                if constexpr (readOnly) {
                    visit(std::as_const(*client));
                } else {
                    visit(*client);
                }
            }
        }
        if constexpr (readOnly) {
            each(self.m_falling.all());
            each(self.m_moving.all());
            each(self.m_items.items());
            each(self.m_stands.stands());
            each(self.m_carts.all());
            each(self.m_orbs.orbs());
            each(self.m_projectiles.all());
            each(self.m_explosives.all());
            each(self.m_hangings.all());
        } else {
            each(self.m_falling.editableAll());
            each(self.m_moving.editableAll());
            each(self.m_items.editableItems());
            each(self.m_stands.editableStands());
            each(self.m_carts.editableAll());
            each(self.m_orbs.editableOrbs());
            each(self.m_projectiles.editableAll());
            each(self.m_explosives.editableAll());
            each(self.m_hangings.editableAll());
        }
    }

    std::shared_ptr<const QcContentRegistry> m_content;
    const Clients &m_clients;
    quint32 m_nextId = 1;
    QcMobManager m_creatures;
    QcItemEntityManager m_items;
    QcFallingBlockManager m_falling;
    QcArmourStandManager m_stands;
    QcMovingBlocks m_moving;
    QcMinecartManager m_carts;
    QcExperienceOrbManager m_orbs;
    QcProjectileManager m_projectiles;
    QcPrimedExplosives m_explosives;
    QcHangingManager m_hangings;
    QSet<quint32> m_died;
    // The loadedEpoch of the last settle; none before the first.
    std::optional<quint64> m_settledEpoch;
};

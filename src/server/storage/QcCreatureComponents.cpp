#include "server/storage/QcCreatureComponents.h"

#include "server/storage/QcEntityCodec.h"
#include "server/storage/QcVillagerCodec.h"

namespace {

using Component = QcEntityCodec::Component;
using QcEntityCodec::kindVersion;
using QcEntityCodec::maxTypeName;

// The current versions of the creature components beyond Creature (Component::Effects, Laying).
constexpr quint16 effectsVersion = 1;
constexpr quint16 layingVersion = 1;

// A component this version reads, by its id, with its reader (`read(stream, version)`).
QcComponentFrames::Known known(Component id, quint16 version, QcComponentFrames::FieldReader read)
{
    return {static_cast<quint16>(id), version, read};
}

// Adds the components that set flags or a single field of the creature; returns `components`.
[[nodiscard]] QcComponentFrames::Writer &addMarks(QcComponentFrames::Writer &components, const QcMobManager::Stored &mob)
{
    if (mob.confined) {
        components.add(static_cast<quint16>(Component::Confined), kindVersion, [](QDataStream &) {});
    }
    if (mob.noAi) {
        components.add(static_cast<quint16>(Component::NoAi), kindVersion, [](QDataStream &) {});
    }
    if (mob.playerBuilt) {
        components.add(static_cast<quint16>(Component::Golem), kindVersion, [&mob](QDataStream &out) { out << mob.playerBuilt; });
    }
    // Only a creature whose laying timer runs (a timer below 0 is chosen anew when it wakes).
    if (mob.layTimer >= 0.0) {
        components.add(static_cast<quint16>(Component::Laying), layingVersion, [&mob](QDataStream &out) { out << mob.layTimer; });
    }
    if (mob.parentUid != 0) {
        components.add(static_cast<quint16>(Component::Family), kindVersion, [&mob](QDataStream &out) {
            const QcChunkPos chunk = mob.parentChunk.value_or(QcChunkPos{});
            out << mob.parentUid << mob.parentChunk.has_value() << static_cast<qint32>(chunk.x) << static_cast<qint32>(chunk.z);
        });
    }
    return components;
}

} // namespace

namespace QcCreatureComponents {

std::optional<Read> read(QDataStream &stream, QcMobManager::Stored mob)
{
    const auto body = [&mob](QDataStream &in, quint16) {
        in >> mob.health >> mob.breedCooldown >> mob.growUp >> mob.bred;
        return true;
    };
    const auto villager = [&mob](QDataStream &in, quint16 version) {
        mob.villager = QcVillagerCodec::read(in, version);
        return mob.villager.has_value();
    };
    const auto effects = [&mob](QDataStream &in, quint16) {
        const std::optional<QList<QcStatusEffect>> read = QcStatusEffects::read(in);
        mob.effects = read.value_or(QList<QcStatusEffect>());
        return read.has_value();
    };
    const auto laying = [&mob](QDataStream &in, quint16) {
        in >> mob.layTimer;
        return true;
    };
    QString carried;
    const auto carriedBlock = [&carried, &mob](QDataStream &in, quint16) {
        const std::optional<QString> read = QcComponentFrames::readText(in, maxTypeName);
        carried = read.value_or(QString());
        in >> mob.keptForBlock;
        return read.has_value();
    };
    const auto confined = [&mob](QDataStream &, quint16) {
        mob.confined = true;
        return true;
    };
    const auto noAi = [&mob](QDataStream &, quint16) {
        mob.noAi = true;
        return true;
    };
    const auto golem = [&mob](QDataStream &in, quint16) {
        in >> mob.playerBuilt;
        return true;
    };
    const auto family = [&mob](QDataStream &in, quint16) {
        bool known = false;
        qint32 x = 0;
        qint32 z = 0;
        in >> mob.parentUid >> known >> x >> z;
        mob.parentChunk = known ? std::optional<QcChunkPos>(QcChunkPos{x, z}) : std::nullopt;
        return mob.parentUid != 0;
    };
    const auto tame = [&mob](QDataStream &in, quint16) {
        QcTameState &state = mob.tame.emplace();
        state.owner = QcComponentFrames::readText(in, maxTypeName).value_or(QString());
        in >> state.sitting >> state.collar;
        // A tame creature without an owner (or whose owner could not be read) cannot be.
        return !state.owner.isEmpty();
    };

    std::optional<QcComponentFrames::ReadComponents> components =
        QcComponentFrames::read(stream, {known(Component::Creature, kindVersion, body), known(Component::Villager, QcVillagerCodec::version, villager),
                                         known(Component::Effects, effectsVersion, effects), known(Component::Carried, kindVersion, carriedBlock),
                                         known(Component::Confined, kindVersion, confined), known(Component::Golem, kindVersion, golem),
                                         known(Component::Laying, layingVersion, laying), known(Component::Tame, kindVersion, tame),
                                         known(Component::NoAi, kindVersion, noAi), known(Component::Family, kindVersion, family)});
    if (!components) {
        return std::nullopt;
    }
    return Read{std::move(mob), std::move(carried), std::move(*components)};
}

QcComponentFrames::Writer &write(QcComponentFrames::Writer &components, const QcMobManager::Stored &mob, const QcContentRegistry &content)
{
    components.add(static_cast<quint16>(Component::Creature), kindVersion,
        [&mob](QDataStream &out) { out << mob.health << mob.breedCooldown << mob.growUp << mob.bred; });
    if (mob.villager) {
        components.add(static_cast<quint16>(Component::Villager), QcVillagerCodec::version,
            [&mob](QDataStream &out) { QcVillagerCodec::write(out, *mob.villager); });
    }
    if (!mob.effects.isEmpty()) {
        components.add(static_cast<quint16>(Component::Effects), effectsVersion, [&mob](QDataStream &out) { QcStatusEffects::write(out, mob.effects); });
    }
    if (mob.carried != qcAirBlockId) {
        const QString state = content.stateName(mob.carried);
        components.add(static_cast<quint16>(Component::Carried), kindVersion, [&state, &mob](QDataStream &out) { out << state << mob.keptForBlock; });
    }
    QcComponentFrames::Writer &marked = addMarks(components, mob);
    if (mob.tame) {
        const QcTameState &tame = *mob.tame;
        marked.add(static_cast<quint16>(Component::Tame), kindVersion, [&tame](QDataStream &out) { out << tame.owner << tame.sitting << tame.collar; });
    }
    return marked;
}

} // namespace QcCreatureComponents

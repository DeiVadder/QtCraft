#include "common/content/registry/QcItemSerializer.h"

#include "common/content/definitions/QcProjectileDefinition.h"
#include "common/content/registry/QcArmourSerializer.h"
#include "common/content/registry/QcBlockSerializer.h"
#include "common/protocol/QcStreamLimits.h"

namespace {

// How the item is used beyond placing and tools: launcher, projectile, throwable, drink, fishing rod.
void writeUses(QDataStream &stream, const QcItemDefinition &item)
{
    // The launcher (decision E-425): the client draws it while the use button is held, only with
    // ammunition (the projectile kind, checked when the content was finished) outside creative mode.
    stream << item.launcher.has_value();
    if (item.launcher) {
        stream << item.launcher->drawSeconds << item.launcher->speed << item.launcher->minPower << item.launcher->ammunition;
    }
    stream << item.projectile;
    // Thrown by hand (decision E-427): the client swings the arm and keeps the cooldown.
    stream << item.throwable.has_value();
    if (item.throwable) {
        stream << item.throwable->speed << item.throwable->cooldownSeconds << item.throwable->inaccuracyDegrees;
    }
    // Drunk with the use button (decision E-500): the client sends ConsumeItem.
    stream << item.drink.has_value();
    if (item.drink) {
        stream << item.drink->clearsEffects;
    }
    // A fishing rod (decision E-503): the client sends its use as ThrowItem; the rest is the server's.
    stream << item.fishing.has_value();
}

// The launcher of an item: an engaged optional holding the launcher, or holding std::nullopt for an
// item without one; std::nullopt when the values are out of the parser's limits.
[[nodiscard]] std::optional<std::optional<QcLauncherDefinition>> readLauncher(QDataStream &stream)
{
    bool launcher = false;
    stream >> launcher;
    if (!launcher) {
        return std::optional<QcLauncherDefinition>();
    }

    QcLauncherDefinition read;
    stream >> read.drawSeconds >> read.speed >> read.minPower >> read.ammunition;
    if (!(read.drawSeconds >= 0.05 && read.drawSeconds <= 10.0) || !(read.minPower >= 0.0 && read.minPower <= 1.0)
        || !(read.speed >= 1.0 && read.speed <= QcProjectileDefinition::maxSpeed) || read.ammunition.size() > 64) {
        return std::nullopt;
    }
    return std::optional<QcLauncherDefinition>(read);
}

// What throwing the item by hand does: an engaged optional holding the throwable, or holding
// std::nullopt for an item that cannot be thrown; std::nullopt when the values are out of the
// parser's limits.
[[nodiscard]] std::optional<std::optional<QcThrowableDefinition>> readThrowable(QDataStream &stream)
{
    bool throwable = false;
    stream >> throwable;
    if (!throwable) {
        return std::optional<QcThrowableDefinition>();
    }

    QcThrowableDefinition read;
    stream >> read.speed >> read.cooldownSeconds >> read.inaccuracyDegrees;
    if (!(read.cooldownSeconds >= 0.0 && read.cooldownSeconds <= 60.0) || !(read.speed >= 1.0 && read.speed <= QcProjectileDefinition::maxSpeed)
        || !(read.inaccuracyDegrees >= 0.0 && read.inaccuracyDegrees <= 45.0)) {
        return std::nullopt;
    }
    return std::optional<QcThrowableDefinition>(read);
}

// `item` with the uses written by writeUses read from `stream`; std::nullopt for corrupt data.
[[nodiscard]] std::optional<QcItemDefinition> withUses(QDataStream &stream, QcItemDefinition item)
{
    std::optional<std::optional<QcLauncherDefinition>> launcher = readLauncher(stream);
    if (!launcher) {
        return std::nullopt;
    }
    item.launcher = *launcher;
    stream >> item.projectile;
    std::optional<std::optional<QcThrowableDefinition>> throwable = readThrowable(stream);
    if (!throwable) {
        return std::nullopt;
    }
    item.throwable = *throwable;

    bool drink = false;
    stream >> drink;
    if (drink) {
        stream >> item.drink.emplace().clearsEffects;
    }
    bool fishing = false;
    stream >> fishing;
    if (fishing) {
        item.fishing.emplace();
    }
    return item;
}

} // namespace

namespace QcItemSerializer {

void write(QDataStream &stream, const QcItemDefinition &item)
{
    stream << item.name << item.translationKey << item.texture << qint32(item.maxStack) << item.placesBlock << item.tool
           << qint32(item.toolLevel) << item.toolSpeed << qint32(item.durability) << item.attackDamage << qint32(item.food)
           << item.fuelSeconds << quint32(item.tags.size());
    for (const QString &tag : item.tags) {
        stream << tag;
    }
    stream << item.scoops << item.emptiesTo << item.bottles << item.placesStand << item.cartBlock << item.ignites;
    QcArmourSerializer::write(stream, item.armour);
    writeUses(stream, item);
}

std::optional<QcItemDefinition> read(QDataStream &stream)
{
    QcItemDefinition item;
    qint32 maxStack = 0;
    qint32 toolLevel = 0;
    qint32 durability = 0;
    qint32 food = 0;
    stream >> item.name >> item.translationKey >> item.texture >> maxStack >> item.placesBlock >> item.tool >> toolLevel
        >> item.toolSpeed >> durability >> item.attackDamage >> food >> item.fuelSeconds;
    const std::optional<quint32> tagCountRead = QcStreamLimits::readCount(stream, 16);
    if (!tagCountRead) {
        return std::nullopt;
    }
    const quint32 tagCount = *tagCountRead;

    for (quint32 t = 0; t < tagCount; ++t) {
        QString tag;
        stream >> tag;
        item.tags.append(tag);
    }

    stream >> item.scoops >> item.emptiesTo >> item.bottles >> item.placesStand >> item.cartBlock >> item.ignites;
    std::optional<std::optional<QcArmourDefinition>> armour = QcArmourSerializer::read(stream);
    if (!armour) {
        return std::nullopt;
    }
    item.armour = std::move(*armour);
    item.maxStack = maxStack;
    item.toolLevel = toolLevel;
    item.durability = durability;
    item.food = food;

    std::optional<QcItemDefinition> used = withUses(stream, std::move(item));
    if (!used || stream.status() != QDataStream::Ok || food < 0 || food > QcItemDefinition::maxFood
        || !QcBlockSerializer::plausibleFuel(used->fuelSeconds)) {
        return std::nullopt;
    }
    return used;
}

} // namespace QcItemSerializer

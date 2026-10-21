#include "common/content/registry/QcItemSerializer.h"

#include "common/content/definitions/QcProjectileDefinition.h"
#include "common/content/registry/QcArmourSerializer.h"
#include "common/content/registry/QcBlockSerializer.h"
#include "common/content/registry/QcHoldSerializer.h"
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
    // The dial the client draws while the item is held (decision E-769).
    stream << quint8(item.dial);
    // A map (decision E-804): the client sends the use of an empty one and draws a filled one.
    stream << quint8(item.map);
    // A portable container (a backpack, decision E-832): the client sends its use in the air as
    // ConsumeItem and lists its contents in the tooltip.
    stream << item.portableContainer;
}

// What the item hangs on a block face (decision E-790): the client targets and draws frames and
// paintings by it.
void writeHanging(QDataStream &stream, const std::optional<QcHangingDefinition> &hangs)
{
    stream << hangs.has_value();
    if (!hangs) {
        return;
    }
    stream << quint8(hangs->type) << hangs->back << quint32(hangs->motifs.size());
    for (const QcPaintingMotif &motif : hangs->motifs) {
        stream << motif.name << motif.texture << qint32(motif.width) << qint32(motif.height);
    }
}

// An engaged optional holding the hanging definition or std::nullopt; std::nullopt for corrupt data.
[[nodiscard]] std::optional<std::optional<QcHangingDefinition>> readHanging(QDataStream &stream)
{
    bool hangs = false;
    stream >> hangs;
    if (!hangs) {
        return std::optional<QcHangingDefinition>();
    }

    QcHangingDefinition read;
    quint8 type = 0;
    stream >> type >> read.back;
    const std::optional<quint32> count = QcStreamLimits::readCount(stream, QcHangingDefinition::maxMotifs);
    if (type >= qcHangingTypeCount || !count) {
        return std::nullopt;
    }
    read.type = QcHangingType(type);
    for (quint32 i = 0; i < *count; ++i) {
        QcPaintingMotif motif;
        qint32 width = 0;
        qint32 height = 0;
        stream >> motif.name >> motif.texture >> width >> height;
        if (width < 1 || height < 1 || width > QcPaintingMotif::maxSide || height > QcPaintingMotif::maxSide) {
            return std::nullopt;
        }
        motif.width = width;
        motif.height = height;
        motif.nameTiles();
        read.motifs.append(motif);
    }
    return std::optional<QcHangingDefinition>(read);
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
    quint8 dial = 0;
    stream >> dial;
    if (dial >= quint8(qcItemDialCount)) {
        stream.setStatus(QDataStream::ReadCorruptData);
        return std::nullopt;
    }
    item.dial = QcItemDial(dial);
    quint8 map = 0;
    stream >> map;
    if (map >= quint8(qcMapItemCount)) {
        stream.setStatus(QDataStream::ReadCorruptData);
        return std::nullopt;
    }
    item.map = QcMapItem(map);
    stream >> item.portableContainer;
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
    writeHanging(stream, item.hangs);
    QcHoldSerializer::write(stream, item.hold);
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
    std::optional<std::optional<QcHangingDefinition>> hangs = readHanging(stream);
    if (!hangs) {
        return std::nullopt;
    }
    std::optional<QcHoldDisplay> hold = QcHoldSerializer::read(stream);
    if (!hold) {
        return std::nullopt;
    }
    if (used) {
        used->hangs = std::move(*hangs);
        used->hold = *hold;
    }
    if (!used || stream.status() != QDataStream::Ok || food < 0 || food > QcItemDefinition::maxFood
        || !QcBlockSerializer::plausibleFuel(used->fuelSeconds)) {
        return std::nullopt;
    }
    return used;
}

} // namespace QcItemSerializer

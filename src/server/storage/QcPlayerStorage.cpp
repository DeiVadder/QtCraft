#include "QcPlayerStorage.h"

#include "QcSaveFile.h"
#include "QcStreamLimits.h"
#include "QcWorldConstants.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// Version 5: position and motion, view, health, inventory, food, armour, game mode, flight, the
// kept inventory of the other mode and the experience (level and points, decision E-398). Files of older development versions are refused (decision E-356).
constexpr quint16 playerVersion = 5;
constexpr QByteArrayView playerMagic("QCPL");

QString sanitizedPlayerName(const QString &name)
{
    QString result;
    for (const QChar c : name) {
        result.append(c.isLetterOrNumber() || c == u'_' || c == u'-' ? c : u'_');
    }
    return result.isEmpty() ? u"player"_s : result.left(32);
}

} // namespace

QcPlayerStorage::QcPlayerStorage(QString directory, std::shared_ptr<const QcContentRegistry> content)
    : m_directory(std::move(directory))
    , m_content(std::move(content))
{
}

QString QcPlayerStorage::fileName(const QString &playerName)
{
    // Readable part in lower case plus a hash of the exact name (decision E-093): names differing
    // only in case or in replaced characters get files that differ on case-insensitive file systems.
    const QByteArray hash = QCryptographicHash::hash(playerName.toUtf8(), QCryptographicHash::Sha256).toHex().left(12);
    return u"%1-%2.qcp"_s.arg(sanitizedPlayerName(playerName).toLower(), QString::fromLatin1(hash));
}

QString QcPlayerStorage::path(const QString &playerName) const
{
    return QDir(m_directory).filePath(u"players/"_s + fileName(playerName));
}

QString QcPlayerStorage::relativePath(const QString &playerName)
{
    return u"players/"_s + fileName(playerName);
}

QcStatus QcPlayerStorage::write(const QString &playerName, const QcPlayerData &player) const
{
    if (!QDir().mkpath(QDir(m_directory).filePath(u"players"_s))) {
        return qcError(u"Cannot create the players directory in %1"_s.arg(m_directory));
    }
    QSaveFile file(path(playerName));
    const QByteArray data = bytes(player);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        return qcError(u"Cannot write %1: %2"_s.arg(path(playerName), file.errorString()));
    }
    return {};
}

QByteArray QcPlayerStorage::bytes(const QcPlayerData &player) const
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QcSaveFile::streamVersion);
    const QcEntityMotion &m = player.motion;
    stream << m.position.x << m.position.y << m.position.z << m.velocity.x << m.velocity.y << m.velocity.z << m.onGround
           << player.yaw << player.pitch << player.health << qint32(player.inventory.selectedHotbarSlot());
    writeSlots(stream, player.inventory);
    stream << qint32(player.food) << player.exhaustion;
    writeEquipment(stream, player.equipment);

    stream << quint8(player.gameMode) << m.flying << qint32(player.keptInventory.selectedHotbarSlot());
    writeSlots(stream, player.keptInventory);
    writeEquipment(stream, player.keptEquipment);
    stream << qint32(player.experience.level()) << qint32(player.experience.points());
    return QcSaveFile::bytes(playerMagic, payload, playerVersion);
}

void QcPlayerStorage::writeSlots(QDataStream &stream, const QcInventory &inventory) const
{
    stream << quint32(QcInventory::slotCount);
    for (int i = 0; i < QcInventory::slotCount; ++i) {
        QcSaveFile::writeStack(stream, inventory.slot(i), *m_content);
    }
}

void QcPlayerStorage::writeEquipment(QDataStream &stream, const QcEquipment &equipment) const
{
    stream << quint32(QcEquipment::slotCount);
    for (const QcItemStack &stack : equipment.stacks) {
        QcSaveFile::writeStack(stream, stack, *m_content);
    }
}

QcResult<std::optional<QcPlayerData>> QcPlayerStorage::read(const QString &playerName, QStringList &warnings) const
{
    QFile stored(path(playerName));
    if (!stored.exists()) {
        return std::optional<QcPlayerData>();
    }
    if (!stored.open(QIODevice::ReadOnly)) {
        return qcError(u"Cannot open %1: %2"_s.arg(stored.fileName(), stored.errorString()));
    }
    return parse(playerName, stored.read(qint64(16) * 1024 * 1024), warnings);
}

QcResult<std::optional<QcPlayerData>> QcPlayerStorage::parse(const QString &playerName, const std::optional<QByteArray> &contents,
                                                             QStringList &warnings) const
{
    const QString file = path(playerName);
    if (!contents) {
        return std::optional<QcPlayerData>();
    }

    quint16 version = 0;
    const QcResult<QByteArray> payload = QcSaveFile::parse(*contents, file, playerMagic, playerVersion, &version);
    if (!payload) {
        return qcError(payload.error());
    }
    if (version < playerVersion) {
        return qcError(u"%1 is from an older development version"_s.arg(file));
    }

    QDataStream stream(*payload);
    stream.setVersion(QcSaveFile::streamVersion);

    QcPlayerData player;
    QcEntityMotion &m = player.motion;
    qint32 selected = 0;
    stream >> m.position.x >> m.position.y >> m.position.z >> m.velocity.x >> m.velocity.y >> m.velocity.z >> m.onGround
        >> player.yaw >> player.pitch >> player.health >> selected;
    if (!readSlots(stream, file, player.inventory, warnings)) {
        return qcError(u"%1 is damaged (inventory)"_s.arg(file));
    }
    player.inventory.setSelectedHotbarSlot(selected);

    qint32 food = 0;
    stream >> food >> player.exhaustion;
    player.food = std::clamp(food, 0, QcItemDefinition::maxFood);
    if (!readEquipment(stream, file, player.equipment, player.inventory, warnings)) {
        return qcError(u"%1 is damaged (armour)"_s.arg(file));
    }
    if (!readGameMode(stream, file, player, warnings)) {
        return qcError(u"%1 is damaged (game mode)"_s.arg(file));
    }
    qint32 level = 0;
    qint32 points = 0;
    stream >> level >> points;
    player.experience = QcExperience(level, points);

    if (stream.status() != QDataStream::Ok || !stream.atEnd() || !isPlausible(player)) {
        return qcError(u"%1 is damaged"_s.arg(file));
    }

    player.health = std::clamp(player.health, 0.0, 1000.0);
    player.exhaustion = std::max(0.0, player.exhaustion);
    player.yaw = std::fmod(player.yaw, 360.0f);
    return std::optional<QcPlayerData>(player);
}

bool QcPlayerStorage::isPlausible(const QcPlayerData &player)
{
    const auto finite = [](const QcVec3 &v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    const QcEntityMotion &m = player.motion;
    constexpr double horizontalLimit = QcWorld::maxHorizontalBlock;
    constexpr double verticalMargin = 64.0;
    // A player saved while falling through the void may be far below it, down to where bodies stop
    // sinking (decision E-288).
    const bool inWorld = finite(m.position) && std::abs(m.position.x) <= horizontalLimit && std::abs(m.position.z) <= horizontalLimit
                         && m.position.y >= QcWorld::voidFloorY - verticalMargin && m.position.y <= QcWorld::worldHeight + verticalMargin;
    const bool directionOk = std::isfinite(player.yaw) && std::isfinite(player.pitch) && std::abs(player.pitch) <= 90.0f;
    return inWorld && finite(m.velocity) && directionOk && std::isfinite(player.health) && std::isfinite(player.exhaustion);
}

bool QcPlayerStorage::readSlots(QDataStream &stream, const QString &path, QcInventory &inventory, QStringList &warnings) const
{
    quint32 slotCount = 0;
    if (!QcStreamLimits::readCount(stream, QcInventory::slotCount, slotCount)) {
        return false;
    }

    for (quint32 i = 0; i < slotCount; ++i) {
        QString name;
        qint32 count = 0;
        qint32 damage = 0;
        stream >> name >> count >> damage;
        if (name.isEmpty() || count <= 0) {
            continue;
        }

        const std::optional<QcItemId> item = m_content->itemId(name);
        if (!item) {
            warnings.append(u"%1: unknown item '%2' dropped"_s.arg(path, name));
            continue;
        }

        const QcItemDefinition &definition = m_content->item(*item);
        inventory.setSlot(int(i), {*item, std::clamp(count, 1, definition.maxStack),
                                   std::clamp(damage, 0, std::max(0, definition.durability - 1))});
    }
    return stream.status() == QDataStream::Ok;
}

bool QcPlayerStorage::readEquipment(QDataStream &stream, const QString &path, QcEquipment &equipment, QcInventory &inventory,
                                    QStringList &warnings) const
{
    quint32 count = 0;
    if (!QcStreamLimits::readCount(stream, QcEquipment::slotCount, count)) {
        return false;
    }

    for (quint32 i = 0; i < count; ++i) {
        QString name;
        qint32 stackCount = 0;
        qint32 damage = 0;
        stream >> name >> stackCount >> damage;
        if (name.isEmpty() || stackCount <= 0) {
            continue;
        }

        const std::optional<QcItemId> item = m_content->itemId(name);
        if (!item) {
            warnings.append(u"%1: unknown item '%2' dropped"_s.arg(path, name));
            continue;
        }

        const QcItemDefinition &definition = m_content->item(*item);
        const QcItemStack stack{*item, 1, std::clamp(damage, 0, std::max(0, definition.durability - 1))};
        // A piece a mod moved to another slot (or that is no armour any more) goes into the inventory.
        if (definition.armour && definition.armour->slot == QcEquipmentSlot(i)) {
            equipment.stacks[i] = stack;
        } else if (inventory.add(stack.item, 1, *m_content, stack.damage) > 0) {
            warnings.append(u"%1: '%2' is no armour for its slot and the inventory is full; dropped"_s.arg(path, name));
        }
    }
    return stream.status() == QDataStream::Ok;
}

bool QcPlayerStorage::readGameMode(QDataStream &stream, const QString &path, QcPlayerData &player, QStringList &warnings) const
{
    quint8 mode = 0;
    qint32 keptSelected = 0;
    stream >> mode >> player.motion.flying >> keptSelected;

    // A mode of a newer version is played as survival rather than refusing the player.
    if (const std::optional<QcGameMode> known = QcGameModes::fromValue(mode)) {
        player.gameMode = *known;
    } else {
        warnings.append(u"%1: unknown game mode %2, playing survival"_s.arg(path).arg(mode));
    }

    if (!readSlots(stream, path, player.keptInventory, warnings)) {
        return false;
    }
    player.keptInventory.setSelectedHotbarSlot(keptSelected);
    return readEquipment(stream, path, player.keptEquipment, player.keptInventory, warnings);
}

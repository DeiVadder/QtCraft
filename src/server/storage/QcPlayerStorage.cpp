#include "server/storage/QcPlayerStorage.h"

#include "common/core/QcWorldConstants.h"
#include "common/protocol/QcStreamLimits.h"
#include "server/storage/QcLoadWarnings.h"
#include "server/storage/QcSaveFile.h"
#include "server/storage/QcStackReader.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

// Version 6: position and motion, view, health, inventory, food, armour, game mode, flight, the
// kept inventory of the other mode, the experience (level and points, decision E-398) and the status
// effects (decision E-450); version 7 adds the enchantment seed (decision E-864). Version 5 is read
// without effects, 5 and 6 without seed; files of older development versions are refused (decision E-356).
constexpr quint16 playerVersion = 7;
constexpr quint16 oldestReadVersion = 5;
constexpr QByteArrayView playerMagic("QCPL");

QString sanitizedPlayerName(const QString &name)
{
    QString result;
    for (const QChar c : name) {
        result.append(c.isLetterOrNumber() || c == u'_' || c == u'-' ? c : u'_');
    }
    return result.isEmpty() ? u"player"_s : result.left(32);
}

// Reads the fields of one player file after its header and collects the warnings of reading it:
// unknown items are dropped, an unknown game mode is played as survival.
class PlayerFileReader
{
public:
    PlayerFileReader(QDataStream &stream, const QcContentRegistry &content, const QString &path)
        : m_stream(stream)
        , m_content(content)
        , m_path(path)
        , m_stacks(content, path, m_warnings)
    {
    }

    // The player of a file of `version`; an error names the damaged part.
    [[nodiscard]] QcResult<QcPlayerData> read(quint16 version)
    {
        QcPlayerData player;
        QcEntityMotion &m = player.motion;
        qint32 selected = 0;
        m_stream >> m.position.x >> m.position.y >> m.position.z >> m.velocity.x >> m.velocity.y >> m.velocity.z >> m.onGround
            >> player.yaw >> player.pitch >> player.health >> selected;
        std::optional<QcInventory> inventory = readSlots();
        if (!inventory) {
            return qcError(u"%1 is damaged (inventory)"_s.arg(m_path));
        }
        inventory->setSelectedHotbarSlot(selected);

        qint32 food = 0;
        m_stream >> food >> player.exhaustion;
        player.food = std::clamp(food, 0, QcItemDefinition::maxFood);
        std::optional<Worn> worn = readEquipment(std::move(*inventory));
        if (!worn) {
            return qcError(u"%1 is damaged (armour)"_s.arg(m_path));
        }
        player.inventory = std::move(worn->inventory);
        player.equipment = worn->equipment;
        std::optional<OtherMode> other = readGameMode();
        if (!other) {
            return qcError(u"%1 is damaged (game mode)"_s.arg(m_path));
        }
        player.gameMode = other->gameMode;
        player.motion.flying = other->flying;
        player.keptInventory = std::move(other->kept.inventory);
        player.keptEquipment = other->kept.equipment;

        qint32 level = 0;
        qint32 points = 0;
        m_stream >> level >> points;
        player.experience = QcExperience(level, points);
        if (version >= 6) {
            std::optional<QList<QcStatusEffect>> effects = QcStatusEffects::read(m_stream);
            if (!effects) {
                return qcError(u"%1 is damaged (effects)"_s.arg(m_path));
            }
            player.effects = std::move(*effects);
        }
        if (version >= 7) {
            m_stream >> player.enchantSeed;
        }
        return player;
    }

    [[nodiscard]] QStringList takeWarnings() { return m_warnings.takeTexts(); }

private:
    // The armour slots and the inventory with the pieces that no longer fit their slot.
    struct Worn
    {
        QcEquipment equipment;
        QcInventory inventory;
    };

    // The game mode, flight and the inventory set kept for the modes of the other set.
    struct OtherMode
    {
        QcGameMode gameMode = QcGameMode::Survival;
        bool flying = false;
        Worn kept;
    };

    // Inventory slots after the selected slot; std::nullopt when they are damaged.
    [[nodiscard]] std::optional<QcInventory> readSlots()
    {
        const std::optional<quint32> slotCountRead = QcStreamLimits::readCount(m_stream, QcInventory::slotCount);
        if (!slotCountRead) {
            return std::nullopt;
        }

        QcInventory inventory;
        for (quint32 i = 0; i < *slotCountRead; ++i) {
            inventory.setSlot(int(i), m_stacks.read(m_stream));
        }
        return m_stream.status() == QDataStream::Ok ? std::optional(std::move(inventory)) : std::nullopt;
    }

    // The armour slots; pieces that no longer fit their slot go into `inventory`. std::nullopt when damaged.
    [[nodiscard]] std::optional<Worn> readEquipment(QcInventory inventory)
    {
        const std::optional<quint32> countRead = QcStreamLimits::readCount(m_stream, QcEquipment::slotCount);
        if (!countRead) {
            return std::nullopt;
        }

        Worn worn{{}, std::move(inventory)};
        for (quint32 i = 0; i < *countRead; ++i) {
            const QcItemStack read = m_stacks.read(m_stream);
            if (read.isEmpty()) {
                continue;
            }

            const QcItemDefinition &definition = m_content.item(read.item);
            const QcItemStack stack = read.withCount(1);
            // A piece a mod moved to another slot (or that is no armour any more) goes into the inventory.
            if (definition.armour && definition.armour->slot == QcEquipmentSlot(i)) {
                worn.equipment.stacks[i] = stack;
            } else if (worn.inventory.add(stack, m_content) > 0) {
                m_warnings.add(u"%1: '%2' is no armour for its slot and the inventory is full; dropped"_s.arg(m_path, definition.name));
            }
        }
        return m_stream.status() == QDataStream::Ok ? std::optional(std::move(worn)) : std::nullopt;
    }

    // Game mode, flight and the kept inventory set; std::nullopt when damaged.
    [[nodiscard]] std::optional<OtherMode> readGameMode()
    {
        OtherMode other;
        quint8 mode = 0;
        qint32 keptSelected = 0;
        m_stream >> mode >> other.flying >> keptSelected;

        // A mode of a newer version is played as survival rather than refusing the player.
        if (const std::optional<QcGameMode> known = QcGameModes::fromValue(mode)) {
            other.gameMode = *known;
        } else {
            m_warnings.add(u"%1: unknown game mode %2, playing survival"_s.arg(m_path).arg(mode));
        }

        std::optional<QcInventory> kept = readSlots();
        if (!kept) {
            return std::nullopt;
        }
        kept->setSelectedHotbarSlot(keptSelected);
        std::optional<Worn> worn = readEquipment(std::move(*kept));
        if (!worn) {
            return std::nullopt;
        }
        other.kept = std::move(*worn);
        return other;
    }

    QDataStream &m_stream;
    const QcContentRegistry &m_content;
    const QString &m_path;
    QcLoadWarnings m_warnings;
    QcStackReader m_stacks;
};

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
    QcStatusEffects::write(stream, player.effects);
    stream << player.enchantSeed;
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

QcPlayerStorage::Result QcPlayerStorage::read(const QString &playerName) const
{
    QFile stored(path(playerName));
    if (!stored.exists()) {
        return Read{};
    }
    if (!stored.open(QIODevice::ReadOnly)) {
        return std::unexpected(Failure{u"Cannot open %1: %2"_s.arg(stored.fileName(), stored.errorString()), {}});
    }
    return parse(playerName, stored.read(qint64(16) * 1024 * 1024));
}

QcPlayerStorage::Result QcPlayerStorage::parse(const QString &playerName, const std::optional<QByteArray> &contents) const
{
    const QString file = path(playerName);
    if (!contents) {
        return Read{};
    }

    const QcResult<QcSaveFile::Payload> payload = QcSaveFile::parse(*contents, file, playerMagic, playerVersion);
    if (!payload) {
        return std::unexpected(Failure{payload.error(), {}});
    }
    if (payload->version < oldestReadVersion) {
        return std::unexpected(Failure{u"%1 is from an older development version"_s.arg(file), {}});
    }

    QDataStream stream(payload->bytes);
    stream.setVersion(QcSaveFile::streamVersion);
    PlayerFileReader reader(stream, *m_content, file);
    QcResult<QcPlayerData> player = reader.read(payload->version);
    // What was read before a damaged part was warned about still is (unknown items dropped).
    if (!player) {
        return std::unexpected(Failure{player.error(), reader.takeWarnings()});
    }
    if (stream.status() != QDataStream::Ok || !stream.atEnd() || !isPlausible(*player)) {
        return std::unexpected(Failure{u"%1 is damaged"_s.arg(file), reader.takeWarnings()});
    }

    player->health = std::clamp(player->health, 0.0, 1000.0);
    player->exhaustion = std::max(0.0, player->exhaustion);
    player->yaw = std::fmod(player->yaw, 360.0f);
    return Read{std::move(*player), reader.takeWarnings()};
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

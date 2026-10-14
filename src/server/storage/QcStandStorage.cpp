#include "QcStandStorage.h"

#include "QcArmour.h"
#include "QcNameText.h"
#include "QcProtocolCodec.h"
#include "QcSaveFile.h"
#include "QcStreamLimits.h"
#include "QcWorldConstants.h"

#include <QDir>
#include <QFile>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr QByteArrayView standsMagic("QCAS");
// 1: stage 23 armour; 2: the cape after merging stage 23 (decision E-239).
constexpr quint16 standsVersion = 2;
const QString fileName = u"stands.qce"_s;

bool isValidPosition(const QcVec3 &p)
{
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::abs(p.x) <= QcWorld::maxHorizontalBlock
           && std::abs(p.z) <= QcWorld::maxHorizontalBlock && p.y >= -64.0 && p.y <= QcWorld::worldHeight + 64.0;
}

} // namespace

QcStandStorage::QcStandStorage(QString directory, std::shared_ptr<const QcContentRegistry> content)
    : m_directory(std::move(directory))
    , m_content(std::move(content))
{
}

QcStatus QcStandStorage::write(const QList<QcArmourStandManager::Stand> &stands) const
{
    if (!QDir().mkpath(m_directory)) {
        return qcError(u"Cannot create %1"_s.arg(m_directory));
    }

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QcSaveFile::streamVersion);
    stream << quint32(stands.size());
    for (const QcArmourStandManager::Stand &stand : stands) {
        stream << stand.position.x << stand.position.y << stand.position.z << stand.yaw;
        QcProtocolCodec::writeStandPose(stream, stand.pose);
        stream << stand.name << stand.item;
        QcSaveFile::writeStack(stream, stand.held, *m_content);
        for (const QcItemStack &stack : stand.equipment.stacks) {
            QcSaveFile::writeStack(stream, stack, *m_content);
        }
        stream << stand.cape;
    }
    return QcSaveFile::write(QDir(m_directory).filePath(fileName), standsMagic, payload, standsVersion);
}

QList<QcArmourStandManager::Stand> QcStandStorage::read(QStringList &warnings) const
{
    const QString path = QDir(m_directory).filePath(fileName);
    if (!QFile::exists(path)) {
        return {};
    }

    QcResult<QList<QcArmourStandManager::Stand>> stands = readFile(path, warnings);
    if (stands) {
        return *stands;
    }

    // Stands are decoration: a damaged file costs them, not the world. The file is kept aside, as
    // the next save replaces it.
    const QString backup = keepAside(path);
    warnings.append(backup.isEmpty() ? u"%1; the world loads without armour stands"_s.arg(stands.error())
                                     : u"%1; the world loads without armour stands, the file was kept as %2"_s.arg(stands.error(), backup));
    return {};
}

QcResult<QList<QcArmourStandManager::Stand>> QcStandStorage::readFile(const QString &path, QStringList &warnings) const
{
    quint16 version = 0;
    const QcResult<QByteArray> payload = QcSaveFile::read(path, standsMagic, standsVersion, &version);
    if (!payload) {
        return qcError(payload.error());
    }

    QDataStream stream(*payload);
    stream.setVersion(QcSaveFile::streamVersion);
    quint32 count = 0;
    if (!QcStreamLimits::readCount(stream, QcArmourStandManager::maxStoredStands, count)) {
        return qcError(u"%1 is damaged (stand count)"_s.arg(path));
    }

    QList<QcArmourStandManager::Stand> stands;
    for (quint32 i = 0; i < count; ++i) {
        QcArmourStandManager::Stand stand;
        if (!readStand(stream, version, stand, warnings)) {
            return qcError(u"%1 is damaged (stand %2)"_s.arg(path).arg(i));
        }
        stands.append(stand);
    }
    if (stream.status() != QDataStream::Ok || !stream.atEnd()) {
        return qcError(u"%1 is damaged"_s.arg(path));
    }
    return stands;
}

QString QcStandStorage::keepAside(const QString &path)
{
    // An older copy is never overwritten: the first free name of stands.qce.damaged, .damaged.2, …
    for (int attempt = 1; attempt <= maxDamagedCopies; ++attempt) {
        const QString backup = attempt == 1 ? path + u".damaged"_s : u"%1.damaged.%2"_s.arg(path).arg(attempt);
        if (!QFile::exists(backup)) {
            return QFile::copy(path, backup) ? backup : QString();
        }
    }
    return {};
}

bool QcStandStorage::readStand(QDataStream &stream, quint16 version, QcArmourStandManager::Stand &stand, QStringList &warnings) const
{
    const QString path = QDir(m_directory).filePath(fileName);
    stream >> stand.position.x >> stand.position.y >> stand.position.z >> stand.yaw;
    stand.pose = QcProtocolCodec::readStandPose(stream);
    stream >> stand.name >> stand.item;
    stand.held = readStack(stream, path, warnings);
    for (QcItemStack &stack : stand.equipment.stacks) {
        stack = readStack(stream, path, warnings);
    }
    if (version >= 2) {
        stream >> stand.cape;
    }
    if (stream.status() != QDataStream::Ok || !isValidPosition(stand.position) || !std::isfinite(stand.yaw)) {
        return false;
    }

    stand.yaw = qcNormalizedAngle(stand.yaw);
    stand.name = QcNameText::clean(stand.name, QcArmourStandManager::maxNameLength);
    for (std::size_t slot = 0; slot < QcEquipment::slotCount; ++slot) {
        QcItemStack &stack = stand.equipment.stacks[slot];
        if (!stack.isEmpty() && !QcArmour::fits(*m_content, stack, QcEquipmentSlot(slot))) {
            warnings.append(u"%1: '%2' is no armour for its slot on a stand; dropped"_s.arg(path, m_content->item(stack.item).name));
            stack = {};
        }
    }
    checkCape(stand, path, warnings);
    return true;
}

void QcStandStorage::checkCape(QcArmourStandManager::Stand &stand, const QString &path, QStringList &warnings) const
{
    const QcAppearanceDefinition *cape = stand.cape.isEmpty() ? nullptr : m_content->appearance(stand.cape);
    if (!stand.cape.isEmpty() && (!cape || cape->kind != QcAppearanceDefinition::Kind::Cape)) {
        warnings.append(u"%1: unknown cape '%2' on a stand; dropped"_s.arg(path, stand.cape));
        stand.cape.clear();
    }
}

QcItemStack QcStandStorage::readStack(QDataStream &stream, const QString &path, QStringList &warnings) const
{
    QString name;
    qint32 count = 0;
    qint32 damage = 0;
    stream >> name >> count >> damage;
    if (name.isEmpty() || count <= 0) {
        return {};
    }

    const std::optional<QcItemId> item = m_content->itemId(name);
    if (!item) {
        warnings.append(u"%1: unknown item '%2' dropped"_s.arg(path, name));
        return {};
    }
    const QcItemDefinition &definition = m_content->item(*item);
    return {*item, 1, std::clamp(int(damage), 0, std::max(0, definition.durability - 1))};
}

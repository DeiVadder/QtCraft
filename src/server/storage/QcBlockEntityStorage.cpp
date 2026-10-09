#include "QcBlockEntityStorage.h"

#include "QcSaveFile.h"
#include "QcStreamLimits.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <optional>

using namespace Qt::StringLiterals;

namespace {

constexpr quint16 machinesVersion = 1;
constexpr QByteArrayView machinesMagic("QCMA");
constexpr QByteArrayView containersMagic("QCCO");
constexpr quint16 containersVersion = 1;

// Reads item stacks of one file by name; an unknown item is dropped with one warning per name.
class StackReader
{
public:
    StackReader(const QcContentRegistry &content, const QString &path, QStringList &warnings)
        : m_content(content)
        , m_path(path)
        , m_warnings(warnings)
    {
    }

    // The stack of one slot; empty for an empty slot or an unknown item.
    QcItemStack read(QDataStream &stream)
    {
        QString name;
        qint32 itemCount = 0;
        qint32 damage = 0;
        stream >> name >> itemCount >> damage;
        if (name.isEmpty() || itemCount <= 0) {
            return {};
        }

        const std::optional<QcItemId> item = m_content.itemId(name);
        if (!item) {
            if (!m_unknown.contains(name)) {
                m_unknown.insert(name);
                m_warnings.append(u"%1: unknown item '%2' dropped"_s.arg(m_path, name));
            }
            return {};
        }
        const QcItemDefinition &definition = m_content.item(*item);
        return {*item, std::clamp(itemCount, 1, definition.maxStack), std::clamp(damage, 0, std::max(0, definition.durability - 1))};
    }

private:
    const QcContentRegistry &m_content;
    const QString &m_path;
    QStringList &m_warnings;
    QSet<QString> m_unknown;
};

bool isStorablePosition(QcBlockPos pos)
{
    return pos.isInsideWorldHeight() && std::abs(pos.x) <= QcWorld::maxHorizontalBlock && std::abs(pos.z) <= QcWorld::maxHorizontalBlock;
}

// One machine; std::nullopt when its data is damaged.
std::optional<QcMachineManager::Machine> readMachine(QDataStream &stream, StackReader &stacks)
{
    qint32 x = 0;
    qint32 y = 0;
    qint32 z = 0;
    stream >> x >> y >> z;
    QcMachineManager::Machine machine;
    machine.pos = {x, y, z};
    for (QcItemStack &stack : machine.stacks) {
        stack = stacks.read(stream);
    }
    stream >> machine.burnRemaining >> machine.burnSeconds >> machine.progress;
    const bool timesOk = std::isfinite(machine.burnRemaining) && std::isfinite(machine.burnSeconds) && std::isfinite(machine.progress);
    if (stream.status() != QDataStream::Ok || !timesOk || !isStorablePosition(machine.pos)) {
        return std::nullopt;
    }

    constexpr double maxSeconds = 100000.0;
    machine.burnSeconds = std::clamp(machine.burnSeconds, 0.0, maxSeconds);
    machine.burnRemaining = std::clamp(machine.burnRemaining, 0.0, machine.burnSeconds);
    machine.progress = std::clamp(machine.progress, 0.0, maxSeconds);
    return machine;
}

// One storage block; std::nullopt when its data is damaged.
std::optional<QcContainerManager::Container> readContainer(QDataStream &stream, StackReader &stacks)
{
    qint32 x = 0;
    qint32 y = 0;
    qint32 z = 0;
    quint32 slotCount = 0;
    stream >> x >> y >> z;
    if (!QcStreamLimits::readCount(stream, QcBlockDefinition::maxContainerSlots, slotCount) || slotCount == 0) {
        return std::nullopt;
    }

    QcContainerManager::Container container{{x, y, z}, QList<QcItemStack>(slotCount)};
    for (QcItemStack &stack : container.stacks) {
        stack = stacks.read(stream);
    }
    if (stream.status() != QDataStream::Ok || !isStorablePosition(container.pos)) {
        return std::nullopt;
    }
    return container;
}

} // namespace

QcBlockEntityStorage::QcBlockEntityStorage(QString directory, std::shared_ptr<const QcContentRegistry> content)
    : m_directory(std::move(directory))
    , m_content(std::move(content))
{
}

QcStatus QcBlockEntityStorage::writeMachines(const QList<QcMachineManager::Machine> &machines) const
{
    if (!QDir().mkpath(m_directory)) {
        return qcError(u"Cannot create %1"_s.arg(m_directory));
    }

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QcSaveFile::streamVersion);
    stream << quint32(machines.size());
    for (const QcMachineManager::Machine &machine : machines) {
        stream << qint32(machine.pos.x) << qint32(machine.pos.y) << qint32(machine.pos.z);
        for (const QcItemStack &stack : machine.stacks) {
            QcSaveFile::writeStack(stream, stack, *m_content);
        }
        stream << machine.burnRemaining << machine.burnSeconds << machine.progress;
    }
    return QcSaveFile::write(QDir(m_directory).filePath(u"machines.qce"_s), machinesMagic, payload, machinesVersion);
}

QcResult<QList<QcMachineManager::Machine>> QcBlockEntityStorage::readMachines(QStringList &warnings) const
{
    const QString path = QDir(m_directory).filePath(u"machines.qce"_s);
    if (!QFile::exists(path)) {
        return QList<QcMachineManager::Machine>();
    }
    const QcResult<QByteArray> payload = QcSaveFile::read(path, machinesMagic, machinesVersion);
    if (!payload) {
        return qcError(payload.error());
    }

    QDataStream stream(*payload);
    stream.setVersion(QcSaveFile::streamVersion);
    quint32 count = 0;
    if (!QcStreamLimits::readCount(stream, QcMachineManager::maxMachines, count)) {
        return qcError(u"%1 is damaged (machine count)"_s.arg(path));
    }

    QList<QcMachineManager::Machine> machines;
    QSet<QcBlockPos> positions;
    StackReader stacks(*m_content, path, warnings);
    for (quint32 i = 0; i < count; ++i) {
        const std::optional<QcMachineManager::Machine> machine = readMachine(stream, stacks);
        if (!machine || positions.contains(machine->pos)) {
            return qcError(u"%1 is damaged (machine %2)"_s.arg(path).arg(i));
        }
        positions.insert(machine->pos);
        machines.append(*machine);
    }
    if (stream.status() != QDataStream::Ok || !stream.atEnd()) {
        return qcError(u"%1 is damaged"_s.arg(path));
    }
    return machines;
}

QcStatus QcBlockEntityStorage::writeContainers(const QList<QcContainerManager::Container> &containers) const
{
    if (!QDir().mkpath(m_directory)) {
        return qcError(u"Cannot create %1"_s.arg(m_directory));
    }

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QcSaveFile::streamVersion);
    stream << quint32(containers.size());
    for (const QcContainerManager::Container &container : containers) {
        stream << qint32(container.pos.x) << qint32(container.pos.y) << qint32(container.pos.z) << quint32(container.stacks.size());
        for (const QcItemStack &stack : container.stacks) {
            QcSaveFile::writeStack(stream, stack, *m_content);
        }
    }
    return QcSaveFile::write(QDir(m_directory).filePath(u"containers.qce"_s), containersMagic, payload, containersVersion);
}

QcResult<QList<QcContainerManager::Container>> QcBlockEntityStorage::readContainers(QStringList &warnings) const
{
    const QString path = QDir(m_directory).filePath(u"containers.qce"_s);
    if (!QFile::exists(path)) {
        return QList<QcContainerManager::Container>();
    }
    const QcResult<QByteArray> payload = QcSaveFile::read(path, containersMagic, containersVersion);
    if (!payload) {
        return qcError(payload.error());
    }

    QDataStream stream(*payload);
    stream.setVersion(QcSaveFile::streamVersion);
    quint32 count = 0;
    if (!QcStreamLimits::readCount(stream, QcContainerManager::maxContainers, count)) {
        return qcError(u"%1 is damaged (container count)"_s.arg(path));
    }

    QList<QcContainerManager::Container> containers;
    QSet<QcBlockPos> positions;
    StackReader stacks(*m_content, path, warnings);
    for (quint32 i = 0; i < count; ++i) {
        std::optional<QcContainerManager::Container> container = readContainer(stream, stacks);
        if (!container || positions.contains(container->pos)) {
            return qcError(u"%1 is damaged (container %2)"_s.arg(path).arg(i));
        }
        positions.insert(container->pos);
        containers.append(std::move(*container));
    }
    if (stream.status() != QDataStream::Ok || !stream.atEnd()) {
        return qcError(u"%1 is damaged"_s.arg(path));
    }
    return containers;
}

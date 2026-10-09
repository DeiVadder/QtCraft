#pragma once

#include "QcContainerManager.h"
#include "QcContentRegistry.h"
#include "QcMachineManager.h"
#include "QcResult.h"

#include <QList>
#include <QString>

#include <memory>

// Blocks with contents of a world directory: machines (machines.qce) and storage blocks
// (containers.qce). Items are stored by name.
class QcBlockEntityStorage
{
public:
    QcBlockEntityStorage(QString directory, std::shared_ptr<const QcContentRegistry> content);

    // Empty without error when nothing was saved yet. Unknown items are dropped with a warning.
    QcResult<QList<QcMachineManager::Machine>> readMachines(QStringList &warnings) const;
    QcStatus writeMachines(const QList<QcMachineManager::Machine> &machines) const;

    // Empty without error when nothing was saved yet. Unknown items are dropped with a warning.
    QcResult<QList<QcContainerManager::Container>> readContainers(QStringList &warnings) const;
    QcStatus writeContainers(const QList<QcContainerManager::Container> &containers) const;

private:
    QString m_directory;
    std::shared_ptr<const QcContentRegistry> m_content;
};

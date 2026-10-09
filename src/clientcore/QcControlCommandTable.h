#pragma once

#include "QcControlEntityCommands.h"
#include "QcControlInventoryCommands.h"
#include "QcControlMachineCommands.h"
#include "QcControlMovementCommands.h"
#include "QcControlSessionCommands.h"
#include "QcControlWorldCommands.h"

#include <QHash>
#include <QJsonObject>
#include <QString>

#include <functional>

class QcControlContext;

// The control commands by name, each served by one of the command groups.
class QcControlCommandTable
{
public:
    using Handler = std::function<void(const QJsonObject &request)>;

    explicit QcControlCommandTable(QcControlContext &context);
    Q_DISABLE_COPY_MOVE(QcControlCommandTable)
    ~QcControlCommandTable() = default;

    void add(const QString &name, const Handler &handler);
    // nullptr for an unknown command.
    const Handler *find(const QString &name) const;

private:
    QHash<QString, Handler> m_handlers;
    QcControlSessionCommands m_session;
    QcControlMovementCommands m_movement;
    QcControlWorldCommands m_world;
    QcControlEntityCommands m_entities;
    QcControlInventoryCommands m_inventory;
    QcControlMachineCommands m_machines;
};

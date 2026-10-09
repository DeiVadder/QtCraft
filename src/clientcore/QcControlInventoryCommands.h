#pragma once

#include <QJsonObject>

class QcClientSession;
class QcControlCommandTable;
class QcControlContext;

// Control commands on the inventory and crafting: hotbar selection, slot moves, dropping and the recipe book.
class QcControlInventoryCommands
{
public:
    explicit QcControlInventoryCommands(QcControlContext &context);

    void registerCommands(QcControlCommandTable &table);

private:
    QcClientSession &session() const;

    void select(const QJsonObject &request);
    void inventory(const QJsonObject &request);
    void moveItem(const QJsonObject &request);
    void drop(const QJsonObject &request);
    void recipes(const QJsonObject &request);
    void craft(const QJsonObject &request);
    void takeResult(const QJsonObject &request);
    void closeCrafting(const QJsonObject &request);

    QcControlContext &m_context;
};

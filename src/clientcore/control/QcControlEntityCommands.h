#pragma once

#include <QJsonObject>

class QcClientSession;
class QcControlCommandTable;
class QcControlContext;

// Control commands about entities: listing them, attacking or using an item on a creature, and
// collecting dropped items.
class QcControlEntityCommands
{
public:
    explicit QcControlEntityCommands(QcControlContext &context);

    void registerCommands(QcControlCommandTable &table);

private:
    QcClientSession &session() const;

    void entities(const QJsonObject &request);
    void attack(const QJsonObject &request);
    void useEntity(const QJsonObject &request);
    void collect(const QJsonObject &request);
    // Turns towards the centre of the entity `request` names; fails the request and returns false
    // when there is no such entity nearby.
    bool turnToEntity(const QJsonObject &request);
    // Walks towards the nearest dropped item within `radius`; true when none is left.
    bool walkToNearestItem(double radius, bool timeUp);

    QcControlContext &m_context;
};

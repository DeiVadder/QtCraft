#pragma once

#include "QcItemStack.h"

#include <QJsonObject>
#include <QString>

class QcClientSession;
class QcControlCommandTable;
class QcControlContext;

// Control commands about entities: listing them, attacking or using an item on a creature,
// collecting dropped items, and riding minecarts.
class QcControlEntityCommands
{
public:
    explicit QcControlEntityCommands(QcControlContext &context);

    void registerCommands(QcControlCommandTable &table);

private:
    QcClientSession &session() const;

    void entities(const QJsonObject &request);
    void attack(const QJsonObject &request);
    // The held stack and entity flags when a use started.
    struct UseBefore
    {
        QcItemStack held;
        quint8 flags = 0;
    };

    void useEntity(const QJsonObject &request);
    // Why the use of the held item on the entity did not catch or feed it; empty when it did.
    QString useEntityError(quint32 entityId, UseBefore before) const;
    void collect(const QJsonObject &request);
    // Gets into the minecart `request` names, or out of the one ridden (decision E-382).
    void ride(const QJsonObject &request);
    void leave(const QJsonObject &request);
    // Turns towards the centre of the entity `request` names; fails the request and returns false
    // when there is no such entity nearby.
    [[nodiscard]] bool turnToEntity(const QJsonObject &request);
    // Walks towards the nearest dropped item within `radius`; true when none is left.
    bool walkToNearestItem(double radius, bool timeUp);

    QcControlContext &m_context;
};

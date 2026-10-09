#pragma once

#include <QJsonObject>

class QcClientSession;
class QcControlCommandTable;
class QcControlContext;

// Control commands that turn, walk and wait.
class QcControlMovementCommands
{
public:
    explicit QcControlMovementCommands(QcControlContext &context);

    void registerCommands(QcControlCommandTable &table);

private:
    QcClientSession &session() const;

    void look(const QJsonObject &request);
    void lookAt(const QJsonObject &request);
    // `move`, and `jump` with `isJump`.
    void move(const QJsonObject &request, bool isJump);
    void wait(const QJsonObject &request);
    void waitUntilReady(const QJsonObject &request);
    // Whether the session plays, the 3 x 3 chunks around the player are loaded and the player has settled.
    bool isReady() const;

    QcControlContext &m_context;
};

#pragma once

#include <QJsonObject>

class QcClientSession;
class QcControlCommandTable;
class QcControlContext;

// Control commands about the player and the session: status, view mode, respawn, chat, sounds and loaded packs.
class QcControlSessionCommands
{
public:
    explicit QcControlSessionCommands(QcControlContext &context);

    void registerCommands(QcControlCommandTable &table);

private:
    QcClientSession &session() const;

    void status(const QJsonObject &request);
    void view(const QJsonObject &request);
    void respawn(const QJsonObject &request);
    void chat(const QJsonObject &request);
    void sounds(const QJsonObject &request);
    void mods(const QJsonObject &request);

    QcControlContext &m_context;
};

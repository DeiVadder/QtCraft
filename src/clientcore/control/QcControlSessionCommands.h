#pragma once

#include <QJsonObject>

class QcClientSession;
class QcControlCommandTable;
class QcControlContext;

// Control commands about the player and the session: status, view mode, respawn, chat, sounds, loaded
// packs, the debug data, the players on the server and the own look.
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
    // Every chat line received so far (also while no request ran).
    void chatLog(const QJsonObject &request);
    // Everyone on the server with their operator rights and round trips (QcMsg::PlayerList).
    void players(const QJsonObject &request);
    // Sets the fields given of the own look and answers with the whole look.
    void appearance(const QJsonObject &request);
    void sounds(const QJsonObject &request);
    void mods(const QJsonObject &request);
    // The debug display's data as JSON (decision E-225); subscribes to the server's part first.
    void debug(const QJsonObject &request);
    // The round trip to the server as the client measures it (decision E-886).
    void latency(const QJsonObject &request);

    QcControlContext &m_context;
};

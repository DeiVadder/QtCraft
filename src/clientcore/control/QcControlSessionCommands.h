#pragma once

#include <QJsonObject>

class QcClientSession;
class QcControlCommandTable;
class QcControlContext;

// Control commands about the player and the session: status, view mode, respawn, chat, sounds, loaded
// packs and the debug data.
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
    // The debug display's data as JSON (decision E-225); subscribes to the server's part first.
    void debug(const QJsonObject &request);

    QcControlContext &m_context;
};

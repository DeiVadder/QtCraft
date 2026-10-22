#pragma once

#include "common/protocol/QcAdminMessages.h"

#include <QJsonObject>

#include <optional>

class QcClientSession;
class QcControlCommandTable;
class QcControlContext;

// Control commands of the admin channel (decision E-848), for tests and automation: `admin` sends
// one request and answers with its result and the report it brought, `adminFollow` chooses the
// topics followed, `adminStatus` reports what arrived of them. The bot must be an operator on the
// server; otherwise the server refuses and the command fails.
class QcControlAdminCommands
{
public:
    // Ticks `admin` waits for the server's answer.
    static constexpr int answerTicks = 100;

    explicit QcControlAdminCommands(QcControlContext &context);

    void registerCommands(QcControlCommandTable &table);

    // The request kind of a control name ("kick", "whitelistAdd"); std::nullopt for unknown names.
    [[nodiscard]] static std::optional<QcMsg::AdminRequest::Kind> kindNamed(const QString &name);

private:
    QcClientSession &session() const;

    void admin(const QJsonObject &request);
    void follow(const QJsonObject &request);
    void status(const QJsonObject &request);
    // The answer to request `id` with the report its kind brings.
    QJsonObject resultJson(quint32 id, QcMsg::AdminRequest::Kind kind) const;
    QJsonObject statusJson() const;
    QJsonObject catalogueJson() const;
    QJsonObject listsJson() const;
    QJsonObject settingsJson() const;
    QJsonObject playersJson() const;
    QJsonObject logJson() const;

    QcControlContext &m_context;
};

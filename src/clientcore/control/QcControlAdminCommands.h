// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcAdminMessages.h"

#include <QJsonObject>

#include <optional>

class QcClientSession;
class QcControlCommandTable;
class QcControlContext;

/*! \brief Control commands of the admin channel (decision E-848), for tests and automation:
    \c {admin} sends one request and answers with its result and the report it brought,
    \c {adminFollow} chooses the topics followed, \c {adminStatus} reports what arrived of them.

    The bot must be an operator on the server; otherwise the server refuses and the command
    fails. */
class QcControlAdminCommands
{
public:
    /*! Ticks \c {admin} waits for the server's answer. */
    static constexpr int answerTicks = 100;

    explicit QcControlAdminCommands(QcControlContext &context);

    /*! Adds the commands of this group to \a table. */
    void registerCommands(QcControlCommandTable &table);

    /*! Returns the request kind of the control name \a name ("kick", "whitelistAdd");
        std::nullopt for unknown names. */
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

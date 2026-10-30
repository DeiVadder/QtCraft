// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "common/protocol/QcSessionMessages.h"
#include "server/commands/QcPermission.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

struct QcServerClient;

/*! \brief Who runs a command (decision E-243): a player through the chat or the server console.

    Replies go back to the source as translatable chat lines (decision E-094); the console
    receives the lines and shows their English text. */
class QcCommandSource
{
public:
    /*! Takes the reply lines instead of a player's chat. */
    using Output = std::function<void(const QcMsg::ChatMessage &line)>;

    /*! The name the console has in the log and in messages to players. */
    static constexpr QStringView consoleName = u"Server";

    QcCommandSource(QcServerClient &client, QcPermission permission);
    /*! A player whose replies go to \a output instead of his chat (the admin channel, decision
        E-848). */
    QcCommandSource(QcServerClient &client, QcPermission permission, Output output);
    explicit QcCommandSource(Output output);

    /*! Returns the player's name, or consoleName. */
    QString name() const;
    /*! Returns the source's right. */
    QcPermission permission() const { return m_permission; }
    /*! Returns whether the source's right allows what \a needed asks for. */
    bool may(QcPermission needed) const { return qcAllows(m_permission, needed); }
    /*! Returns the player behind the source; nullptr for the console. */
    QcServerClient *player() const { return m_client; }
    /*! Returns the key of the source's pending confirmation: the player's entity id, 0 for the
        console. */
    quint32 id() const;
    /*! Returns the block the player stands in, origin of ~ coordinates; std::nullopt for the
        console. */
    [[nodiscard]] std::optional<QcBlockPos> origin() const;

    /*! Sends the translatable line \a key with \a arguments; \a key is marked with
        QT_TRANSLATE_NOOP("QcServerMessages", ...). */
    void reply(const char *key, const QStringList &arguments = {}) const;
    /*! Sends \a line as it is, not translated. */
    void replyPlain(const QString &line) const;
    /*! Sends \a line to the output, else to the player's chat; the console without an output
        drops it. */
    void send(const QcMsg::ChatMessage &line) const;

private:
    QcServerClient *m_client = nullptr;
    Output m_output;
    QcPermission m_permission = QcPermission::Player;
};

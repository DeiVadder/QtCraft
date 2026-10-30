// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcSessionMessages.h"

#include <QString>
#include <QStringList>

#include <functional>

/*! \brief Translatable chat lines and disconnect reasons of the server (decision E-094).

    The server sends a key, which is the English text with %1.. placeholders marked with
    QT_TRANSLATE_NOOP in \c {context}, and its arguments; the client translates the key and fills
    in the arguments. The message also carries the filled-in English text for lines without a key
    (player chat, free-form warnings). */
namespace QcChatText {

/*! The translation context of the keys. */
inline constexpr char context[] = "QcServerMessages";

/*! Returns \a pattern with %1 .. %99 replaced by \a arguments in one pass (arguments are not
    scanned again); placeholders without an argument stay as they are. */
QString format(QStringView pattern, const QStringList &arguments);
/*! Returns a keyed line with \a arguments; \a key must be marked with
    QT_TRANSLATE_NOOP("QcServerMessages", ...). */
QcMsg::ChatMessage message(const char *key, const QStringList &arguments = {});
/*! Returns a keyed line whose arguments may stand for content texts
    (QcMsg::ChatMessage::contentKeys, decision E-873): \a contentKeys as long as \a arguments,
    empty entries for plain arguments. */
QcMsg::ChatMessage message(const char *key, const QStringList &arguments, const QStringList &contentKeys);
/*! Returns a keyed line with a count: \a key (marked with QT_TRANSLATE_N_NOOP in \c {context})
    has %n for \a count besides its %1.. \a arguments; the count travels as the last argument,
    and the client picks the plural form of its language. */
QcMsg::ChatMessage counted(const char *key, int count, const QStringList &arguments = {});
/*! The entry of QcMsg::ChatMessage::contentKeys for an argument that is itself a server message
    (\c {key} marked with QT_TRANSLATE_NOOP in \c {context}, the argument its English text): the
    client shows it translated (decision E-911; /help's lines of a command and its description). */
inline constexpr QLatin1StringView messageKeyPrefix{"tr:"};
/*! Returns the entry of QcMsg::ChatMessage::contentKeys for the server message \a key:
    messageKeyPrefix followed by \a key. */
QString messageKey(const char *key);
/*! Returns an untranslated line of \a text (player chat, warnings from files). */
QcMsg::ChatMessage plain(const QString &text);
/*! Returns the text of \a message to show: the translated key with its arguments, or the plain
    text. */
QString display(const QcMsg::ChatMessage &message);
/*! Returns the same with the content texts of the arguments that stand for one: \a contentText
    gives the text of a content key (empty when it knows none, then the argument stays). */
QString display(const QcMsg::ChatMessage &message, const std::function<QString(const QString &)> &contentText);

/*! Returns a Disconnect whose reason is the keyed line of \a key with \a arguments, as message()
    makes it. */
QcMsg::Disconnect disconnect(const char *key, const QStringList &arguments = {});
/*! Returns the reason of \a message to show, as for a chat line. */
QString display(const QcMsg::Disconnect &message);

} // namespace QcChatText

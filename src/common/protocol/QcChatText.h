#pragma once

#include "common/protocol/QcSessionMessages.h"

#include <QString>
#include <QStringList>

#include <functional>

// Translatable chat lines and disconnect reasons of the server (decision E-094). The server sends a key, which is the
// English text with %1.. placeholders marked with QT_TRANSLATE_NOOP in `context`, and its
// arguments; the client translates the key and fills in the arguments. The message also carries
// the filled-in English text for lines without a key (player chat, free-form warnings).
namespace QcChatText {

inline constexpr char context[] = "QcServerMessages";

// Replaces %1 .. %99 by the arguments in one pass (arguments are not scanned again); placeholders
// without an argument stay as they are.
QString format(QStringView pattern, const QStringList &arguments);
// A keyed line; `key` must be marked with QT_TRANSLATE_NOOP("QcServerMessages", ...).
QcMsg::ChatMessage message(const char *key, const QStringList &arguments = {});
// A keyed line whose arguments may stand for content texts (QcMsg::ChatMessage::contentKeys,
// decision E-873): `contentKeys` as long as `arguments`, empty entries for plain arguments.
QcMsg::ChatMessage message(const char *key, const QStringList &arguments, const QStringList &contentKeys);
// An untranslated line (player chat, warnings from files).
QcMsg::ChatMessage plain(const QString &text);
// The text to show: the translated key with its arguments, or the plain text.
QString display(const QcMsg::ChatMessage &message);
// The same with the content texts of the arguments that stand for one: `contentText` gives the text
// of a content key (empty when it knows none, then the argument stays).
QString display(const QcMsg::ChatMessage &message, const std::function<QString(const QString &)> &contentText);

// The same for the reason of a Disconnect.
QcMsg::Disconnect disconnect(const char *key, const QStringList &arguments = {});
QString display(const QcMsg::Disconnect &message);

} // namespace QcChatText

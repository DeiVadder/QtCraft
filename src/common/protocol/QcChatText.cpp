#include "QcChatText.h"

#include <QCoreApplication>

namespace QcChatText {

QString format(QStringView pattern, const QStringList &arguments)
{
    QString result;
    result.reserve(pattern.size());

    for (qsizetype i = 0; i < pattern.size(); ++i) {
        const QChar c = pattern[i];
        const bool firstDigit = c == u'%' && i + 1 < pattern.size() && pattern[i + 1] >= u'1' && pattern[i + 1] <= u'9';
        if (!firstDigit) {
            result.append(c);
            continue;
        }

        // One or two digits.
        int number = pattern[i + 1].digitValue();
        qsizetype length = 2;
        if (i + 2 < pattern.size() && pattern[i + 2].isDigit()) {
            number = number * 10 + pattern[i + 2].digitValue();
            length = 3;
        }

        if (number <= arguments.size()) {
            result.append(arguments[number - 1]);
        } else {
            result.append(pattern.mid(i, length));
        }
        i += length - 1;
    }
    return result;
}

QcMsg::ChatMessage message(const char *key, const QStringList &arguments)
{
    const QString keyText = QString::fromUtf8(key);
    return {format(keyText, arguments), keyText, arguments};
}

QcMsg::ChatMessage plain(const QString &text)
{
    return {text, {}, {}};
}

QString display(const QcMsg::ChatMessage &message)
{
    if (message.key.isEmpty()) {
        return message.text;
    }

    // Without a translation (or for a key this version does not know) the key is the English text.
    const QString translated = QCoreApplication::translate(context, message.key.toUtf8().constData());
    return format(translated, message.arguments);
}

} // namespace QcChatText

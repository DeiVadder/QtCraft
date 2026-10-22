#include <QFile>
#include <QHash>
#include <QSet>
#include <QTest>
#include <QXmlStreamReader>

#include <algorithm>
#include <optional>

using namespace Qt::StringLiterals;

// English is the source language of the interface; its plural texts ("%n chunk(s)") need forms of
// their own, which only translations/qtcraft_en.ts holds (lupdate keeps plural messages there,
// translations/en.json fills them through scripts/apply-translations.py). Without a form for a
// message's context the interface shows the source text with "(s)". The German file is kept up to
// date with every change of the texts, so each of its plural messages has to be in the English
// file as well, in the same context and with every form filled.
class TestEnglishPlurals : public QObject
{
    Q_OBJECT

private:
    struct Message
    {
        QString context;
        QString source;
        QStringList forms;
        bool unfinished = false;
    };

    // The plural messages of a .ts file that lupdate still finds in the sources.
    static QList<Message> pluralMessages(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        QList<Message> messages;
        QString context;
        QXmlStreamReader xml(&file);
        while (!xml.atEnd()) {
            if (xml.readNext() != QXmlStreamReader::StartElement) {
                continue;
            }
            if (xml.name() == "name"_L1) {
                context = xml.readElementText();
            } else if (xml.name() == "message"_L1 && xml.attributes().value("numerus"_L1) == "yes"_L1) {
                const std::optional<Message> message = readMessage(xml, context);
                if (message) {
                    messages.append(*message);
                }
            }
        }
        return messages;
    }

    // One <message>; std::nullopt for one that vanished from the sources or is obsolete.
    static std::optional<Message> readMessage(QXmlStreamReader &xml, const QString &context)
    {
        Message message{context, {}, {}, false};
        bool gone = false;
        while (xml.readNextStartElement()) {
            if (xml.name() == "source"_L1) {
                message.source = xml.readElementText();
            } else if (xml.name() == "translation"_L1) {
                const QStringView type = xml.attributes().value("type"_L1);
                gone = type == "vanished"_L1 || type == "obsolete"_L1;
                message.unfinished = type == "unfinished"_L1;
                while (xml.readNextStartElement()) {
                    message.forms.append(xml.readElementText());
                }
            } else {
                xml.skipCurrentElement();
            }
        }
        return gone ? std::nullopt : std::optional<Message>(message);
    }

private slots:
    void everyPluralHasEnglishForms()
    {
        const QList<Message> german = pluralMessages(QStringLiteral(QTCRAFT_SOURCE_DIR "/translations/qtcraft_de.ts"));
        const QList<Message> english = pluralMessages(QStringLiteral(QTCRAFT_SOURCE_DIR "/translations/qtcraft_en.ts"));
        // The view distance, the player list, texture packs and the inventory at least.
        QVERIFY(german.size() >= 4);

        QHash<std::pair<QString, QString>, Message> byKey;
        for (const Message &message : english) {
            byKey.insert({message.context, message.source}, message);
        }
        QStringList missing;
        for (const Message &message : german) {
            const auto found = byKey.constFind({message.context, message.source});
            const bool filled = found != byKey.cend() && !found->unfinished && found->forms.size() == 2
                                && std::ranges::none_of(found->forms, &QString::isEmpty);
            if (!filled) {
                missing.append(message.context + u": "_s + message.source);
            }
        }
        QVERIFY2(missing.isEmpty(), qPrintable(u"no English plural forms (update_translations, then "
                                                u"scripts/apply-translations.py translations/qtcraft_en.ts translations/en.json):\n"_s
                                               + missing.join(u'\n')));
    }
};

QTEST_GUILESS_MAIN(TestEnglishPlurals)
#include "test_english_plurals.moc"

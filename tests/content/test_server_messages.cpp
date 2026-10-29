// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTest>

using namespace Qt::StringLiterals;

// Every chat line of the server is translatable (decision E-094): each key marked with
// QT_TRANSLATE_NOOP("QcServerMessages", ...) in the server, the commands included, has a German
// translation in translations/de.json.
class TestServerMessages : public QObject
{
    Q_OBJECT

private:
    // The keys of one source file; adjacent string literals are joined like the compiler does.
    static QStringList keysIn(const QString &source)
    {
        static const QRegularExpression marker(u"QT_TRANSLATE_NOOP\\(\\s*\"QcServerMessages\"\\s*,((?:\\s*\"(?:[^\"\\\\]|\\\\.)*\")+)\\s*\\)"_s);
        static const QRegularExpression literal(u"\"((?:[^\"\\\\]|\\\\.)*)\""_s);
        QStringList keys;
        for (const QRegularExpressionMatch &match : marker.globalMatch(source)) {
            QString key;
            for (const QRegularExpressionMatch &part : literal.globalMatch(match.captured(1))) {
                key += part.captured(1);
            }
            keys.append(key.replace(u"\\\""_s, u"\""_s));
        }
        return keys;
    }

private slots:
    void everyServerMessageHasAGermanTranslation()
    {
        QFile file(QStringLiteral(QTCRAFT_SOURCE_DIR "/translations/de.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject german = QJsonDocument::fromJson(file.readAll()).object();
        QVERIFY(!german.isEmpty());

        QStringList keys;
        QDirIterator it(QStringLiteral(QTCRAFT_SOURCE_DIR "/src/server"), {u"*.cpp"_s, u"*.h"_s}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QFile source(it.next());
            QVERIFY(source.open(QIODevice::ReadOnly));
            keys += keysIn(QString::fromUtf8(source.readAll()));
        }

        // The commands alone bring well over a hundred lines.
        QVERIFY(keys.size() > 150);
        QStringList missing;
        for (const QString &key : std::as_const(keys)) {
            if (german.value(key).toString().isEmpty()) {
                missing.append(key);
            }
        }
        QVERIFY2(missing.isEmpty(), qPrintable(missing.join(u'\n')));
    }

    void theScannerJoinsLiterals()
    {
        const QString source = u"x(QT_TRANSLATE_NOOP(\"QcServerMessages\", \"a %1 \"\n   \"b\\\"c\"), QT_TRANSLATE_NOOP(\"Other\", \"no\")))"_s;
        QCOMPARE(keysIn(source), QStringList{u"a %1 b\"c"_s});
    }
};

QTEST_GUILESS_MAIN(TestServerMessages)
#include "test_server_messages.moc"

// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/entitymeshes/QcSignTextCache.h"
#include "clientcore/session/QcChunkExtras.h"
#include "common/content/parser/QcStructureParser.h"
#include "common/protocol/QcProtocol.h"
#include "QcPlayerUseFixture.h"
#include "server/storage/QcBlockEntityCodec.h"
#include "server/storage/QcSaveFile.h"

#include <QJsonDocument>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// The block entities of chunk (0, 0) with one sign entry of `version` at (3, 20, 5): the lines
// "Old" and "Sign", then `rest` (the colour and what a newer version appends), framed as the codec
// frames tables and entries (decision E-440).
QByteArray signBlob(quint16 version, const QByteArray &rest)
{
    const auto bytes = [](const auto &write) {
        QByteArray blob;
        QDataStream out(&blob, QIODevice::WriteOnly);
        out.setVersion(QcSaveFile::streamVersion);
        write(out);
        return blob;
    };
    const QByteArray entry = bytes([&](QDataStream &out) {
        out << qint32{3} << qint32{20} << qint32{5} << quint8{4} << u"Old"_s << u"Sign"_s << u""_s << u""_s;
        out.writeRawData(rest.constData(), static_cast<int>(rest.size()));
    });
    const QByteArray table = bytes([&](QDataStream &out) {
        out << quint32{1} << version << static_cast<quint32>(entry.size());
        out.writeRawData(entry.constData(), static_cast<int>(entry.size()));
    });
    return bytes([&](QDataStream &out) {
        out << quint16{1} << static_cast<quint16>(QcBlockEntityRecord::Table::Signs) << quint16{1} << static_cast<quint32>(table.size());
        out.writeRawData(table.constData(), static_cast<int>(table.size()));
    });
}

// The relative luminance of an sRGB colour (WCAG).
double luminance(int red, int green, int blue)
{
    const auto linear = [](int value) {
        const double c = value / 255.0;
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(red) + 0.7152 * linear(green) + 0.0722 * linear(blue);
}

} // namespace

// Dyed sign text (decision E-830): a dye used on a written sign colours its text and takes one dye
// (none in creative mode, none for the colour it already has); a blank sign opens its editor
// instead; adventure mode and spectators change nothing. The colour is saved with the text, sent
// with it, and the client's quads are built again in the new ink.
class TestSignColours : public QcPlayerUseFixture
{
    Q_OBJECT

private:
    using Lines = QcSignText::Lines;

    // Places an oak sign two blocks north of the player and writes `lines` on it.
    QcBlockPos writtenSign(const Lines &lines)
    {
        hold(u"oak_sign"_s);
        const QcBlockPos sign = m_feet.offset(0, 0, -2);
        face(0.0f);
        send(QcMsg::UseItem{sign.offset(0, -1, 0), QcBlockFace::Up});
        if (!QcSignText::isEmpty(lines)) {
            send(QcMsg::EditSign{sign, lines});
        }
        return sign;
    }

    QString colorAt(QcBlockPos pos) const { return m_server->world().signs().colorAt(pos); }
    int dyes(const QString &name) const { return countOf(baseItem(name)); }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void dyeColoursTheTextAndTakesOneDye()
    {
        join();
        const QcBlockPos sign = writtenSign(Lines{u"Hello"_s});
        QCOMPARE(colorAt(sign), QString());
        const qsizetype editors = m_client->signEditors.size();

        hold(u"blue_dye"_s, 3);
        send(QcMsg::UseItem{sign, QcBlockFace::South});
        ticks(1);
        QCOMPARE(colorAt(sign), u"blue"_s);
        QCOMPARE(dyes(u"blue_dye"_s), 2);
        QCOMPARE(m_client->signTexts.last().color, u"blue"_s);
        QCOMPARE(m_client->signTexts.last().lines[0], u"Hello"_s);
        // Dyeing opens no editor.
        QCOMPARE(m_client->signEditors.size(), editors);

        // The same colour again takes no dye.
        send(QcMsg::UseItem{sign, QcBlockFace::South});
        QCOMPARE(dyes(u"blue_dye"_s), 2);
        QCOMPARE(m_client->signEditors.size(), editors);

        // Black is the default colour.
        hold(u"black_dye"_s, 2);
        send(QcMsg::UseItem{sign, QcBlockFace::South});
        QCOMPARE(colorAt(sign), QString());
        QCOMPARE(dyes(u"black_dye"_s), 1);
        send(QcMsg::UseItem{sign, QcBlockFace::South});
        QCOMPARE(dyes(u"black_dye"_s), 1);

        // A new text keeps the colour.
        hold(u"red_dye"_s);
        send(QcMsg::UseItem{sign, QcBlockFace::South});
        QCOMPARE(colorAt(sign), u"red"_s);
        // The last dye is gone: the empty hand opens the editor.
        send(QcMsg::UseItem{sign, QcBlockFace::South});
        send(QcMsg::EditSign{sign, Lines{u"Changed"_s}});
        QCOMPARE(colorAt(sign), u"red"_s);
    }

    void sneakingOrOutOfReachChangesNothing()
    {
        join();
        const QcBlockPos sign = writtenSign(Lines{u"Hello"_s});
        const qsizetype editors = m_client->signEditors.size();
        hold(u"blue_dye"_s);

        QcPlayerInput sneak;
        sneak.sequence = ++m_sequence;
        sneak.sneak = true;
        send(QcMsg::PlayerInput{sneak});
        send(QcMsg::UseItem{sign, QcBlockFace::South});
        QCOMPARE(colorAt(sign), QString());
        QCOMPARE(dyes(u"blue_dye"_s), 1);
        QCOMPARE(m_client->signEditors.size(), editors);
        QcPlayerInput stand;
        stand.sequence = ++m_sequence;
        send(QcMsg::PlayerInput{stand});

        const QcBlockPos far = m_feet.offset(0, 0, -30);
        m_server->world().setBlock(far, id("qtcraft:oak_sign"));
        m_server->world().signs().set(far, Lines{u"Far"_s});
        send(QcMsg::UseItem{far, QcBlockFace::South});
        QCOMPARE(colorAt(far), QString());
        QCOMPARE(dyes(u"blue_dye"_s), 1);
        QCOMPARE(m_client->signEditors.size(), editors);
    }

    void blankSignOpensItsEditorAndKeepsTheDye()
    {
        join();
        const QcBlockPos sign = writtenSign({});
        const qsizetype editors = m_client->signEditors.size();
        hold(u"red_dye"_s);
        send(QcMsg::UseItem{sign, QcBlockFace::South});
        QCOMPARE(m_client->signEditors.size(), editors + 1);
        QCOMPARE(dyes(u"red_dye"_s), 1);
        QCOMPARE(colorAt(sign), QString());
    }

    void creativeKeepsTheDyeAdventureAndSpectatorsChangeNothing()
    {
        join();
        const QcBlockPos sign = writtenSign(Lines{u"Hello"_s});
        chat(u"/gamemode creative"_s);
        hold(u"green_dye"_s);
        send(QcMsg::UseItem{sign, QcBlockFace::South});
        QCOMPARE(colorAt(sign), u"green"_s);
        QCOMPARE(dyes(u"green_dye"_s), 1);

        hold(u"cyan_dye"_s);
        chat(u"/gamemode adventure"_s);
        send(QcMsg::UseItem{sign, QcBlockFace::South});
        QCOMPARE(colorAt(sign), u"green"_s);
        chat(u"/gamemode spectator"_s);
        send(QcMsg::UseItem{sign, QcBlockFace::South});
        QCOMPARE(colorAt(sign), u"green"_s);
    }

    void colourIsSavedAndComesBackWithItsChunk()
    {
        QcBlockEntityRecord record;
        record.chunk = {0, 0};
        record.signs = {{{3, 20, 5}, Lines{u"One"_s}, u"purple"_s}, {{4, 21, 5}, Lines{}, u"white"_s}, {{5, 21, 5}, Lines{u"Two"_s}, {}}};
        QcLoadWarnings warnings;
        const QcResult<QcBlockEntityRecord> read = QcBlockEntityCodec::decode(QcBlockEntityCodec::encode(record, *m_content), {0, 0}, *m_content, warnings);
        QVERIFY2(read.has_value(), qPrintable(read ? QString() : read.error()));
        QCOMPARE(read->signs, record.signs);
        // A colour longer than any save holds damages the record.
        QcBlockEntityRecord damaged = record;
        damaged.signs[0].color = u"x"_s.repeated(40);
        QVERIFY(!QcBlockEntityCodec::decode(QcBlockEntityCodec::encode(damaged, *m_content), {0, 0}, *m_content, warnings));

        m_unloadChunks = true;
        join();
        const QcBlockPos far = m_feet.offset(160, 0, 0);
        const QcBlockPos unknown = far.offset(1, 0, 0);
        m_server->world().loadChunk(far.chunk());
        m_server->world().setBlock(far, id("qtcraft:oak_sign"));
        m_server->world().setBlock(unknown, id("qtcraft:oak_sign"));
        m_server->world().signs().set(far, Lines{u"Kept"_s});
        m_server->world().signs().setColor(far, u"orange"_s);
        m_server->world().signs().set(unknown, Lines{u"Odd"_s});
        m_server->world().signs().setColor(unknown, u"chartreuse"_s);
        ticks(200);
        QVERIFY(!m_server->world().chunk(far.chunk()));
        m_server->world().loadChunk(far.chunk());
        QCOMPARE(colorAt(far), u"orange"_s);
        // An unknown colour comes back as the default.
        QCOMPARE(colorAt(unknown), QString());
        QVERIFY(m_server->world().signs().at(unknown));
    }

    void olderAndNewerSignEntriesAreRead()
    {
        join();
        QcLoadWarnings warnings;
        // Version 1 (before decision E-830): the lines only, the default colour.
        const QcResult<QcBlockEntityRecord> old = QcBlockEntityCodec::decode(signBlob(1, {}), {0, 0}, *m_content, warnings);
        QVERIFY2(old.has_value(), qPrintable(old ? QString() : old.error()));
        QCOMPARE(old->signs.size(), 1);
        QCOMPARE(old->signs[0].lines[1], u"Sign"_s);
        QCOMPARE(old->signs[0].color, QString());

        // Version 3: the colour is read, what follows it is kept and written back unchanged.
        QByteArray rest;
        QDataStream out(&rest, QIODevice::WriteOnly);
        out.setVersion(QcSaveFile::streamVersion);
        out << u"cyan"_s;
        rest += "zz";
        const QByteArray blob = signBlob(3, rest);
        const QcResult<QcBlockEntityRecord> newer = QcBlockEntityCodec::decode(blob, {0, 0}, *m_content, warnings);
        QVERIFY2(newer.has_value(), qPrintable(newer ? QString() : newer.error()));
        QCOMPARE(newer->signs.size(), 1);
        QCOMPARE(newer->signs[0].color, u"cyan"_s);
        const QcEntityComponents tail{{static_cast<quint16>(QcBlockEntityRecord::Table::Signs), 3, true, "zz"}};
        QCOMPARE(newer->kept.entries.value({3, 20, 5}), tail);
        QCOMPARE(QcBlockEntityCodec::encode(*newer, *m_content), blob);
    }

    void structuresGiveValidColoursOnly()
    {
        const auto parse = [](const QByteArray &sign) {
            const QByteArray text = QByteArray("{\"name\": \"a\", \"showcase\": {\"order\": 1}, \"palette\": {\"#\": \"oak_sign\"}, "
                                               "\"layers\": [[\"#\"]], \"signs\": [")
                                    + sign + "]}";
            return QcStructureParser::parseStructure(QJsonDocument::fromJson(text).object(), u"qtcraft"_s);
        };
        const QcResult<QcStructureDefinition> blue = parse(R"({"at": [0, 0, 0], "lines": ["Hi"], "color": "blue"})");
        QVERIFY2(blue, qPrintable(blue ? QString() : blue.error()));
        QCOMPARE(blue->signs[0].color, u"blue"_s);
        const QcResult<QcStructureDefinition> plain = parse(R"({"at": [0, 0, 0], "lines": ["Hi"]})");
        QVERIFY(plain);
        QCOMPARE(plain->signs[0].color, QString());
        QVERIFY(!parse(R"({"at": [0, 0, 0], "lines": ["Hi"], "color": "chartreuse"})"));
        const QcResult<QcStructureDefinition> number = parse(R"({"at": [0, 0, 0], "lines": ["Hi"], "color": 4})");
        QVERIFY(!number);
        QVERIFY2(number.error().contains(u"color"_s), qPrintable(number.error()));
    }

    void everyInkStandsOffBirch()
    {
        // Birch planks (#d2bd88), the lightest wood: every ink is at least 2:1 darker.
        const double birch = luminance(0xd2, 0xbd, 0x88);
        const QStringList colours{u"white"_s, u"orange"_s, u"magenta"_s, u"light_blue"_s, u"yellow"_s, u"lime"_s, u"pink"_s, u"gray"_s,
                                  u"light_gray"_s, u"cyan"_s, u"purple"_s, u"blue"_s, u"brown"_s, u"green"_s, u"red"_s, u"black"_s};
        for (const QString &colour : colours) {
            const QcSignText::Ink ink = QcSignText::inkOf(colour);
            const double contrast = (birch + 0.05) / (luminance(ink.red, ink.green, ink.blue) + 0.05);
            QVERIFY2(contrast >= 2.0, qPrintable(u"%1: %2"_s.arg(colour).arg(contrast)));
        }
        // Yellow stays yellowish: more red and green than blue.
        const QcSignText::Ink yellow = QcSignText::inkOf(u"yellow");
        QVERIFY(yellow.green > yellow.blue + 60);
    }

    void protocolCarriesTheColour()
    {
        const QcMsg::SignText text{{17, 30, -5}, Lines{u"a"_s}, u"light_blue"_s};
        const QcResult<QcMessage> decoded = QcProtocol::decode(QcProtocol::encode(QcMessage(text)), QcProtocol::Peer::Server);
        QVERIFY(decoded && std::holds_alternative<QcMsg::SignText>(*decoded));
        QCOMPARE(std::get<QcMsg::SignText>(*decoded).color, text.color);
        QCOMPARE(std::get<QcMsg::SignText>(*decoded).lines, text.lines);
        const QcMsg::SignText tooLong{{1, 2, 3}, Lines{u"a"_s}, u"x"_s.repeated(40)};
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMessage(tooLong)), QcProtocol::Peer::Server));

        // The client keeps a sign with a colour but no lines; blank ones go.
        QcChunkExtras extras;
        extras.setSign({{1, 2, 3}, Lines{}, u"red"_s});
        QCOMPARE(extras.signs().size(), 1);
        extras.setSign({{1, 2, 3}, Lines{}, {}});
        QVERIFY(extras.signs().isEmpty());
    }

    void quadsAreBuiltAgainInTheNewInk()
    {
        buildArea();
        QcSignTextCache cache;
        const QcEntityMesher::UvLookup uv = [](const QString &) { return QRectF(0.0, 0.0, 1.0, 1.0); };
        const QcBlockPos pos{1, 64, 1};
        const QcBlockId oak = id("qtcraft:oak_sign");
        const QcEntityLight light(1.0f, 0.0f);
        QHash<QcBlockPos, QcSignText::Text> signs{{pos, {Lines{u"Hi"_s}, {}}}};
        cache.setSigns(signs, 1);
        const QcSignText::Ink black = QcSignText::inkOf({});
        QCOMPARE(cache.quads(pos, oak, light, *m_content, uv).front()[0].tint[2], black.blue);

        signs[pos].color = u"blue"_s;
        cache.setSigns(signs, 2);
        const QcSignText::Ink blue = QcSignText::inkOf(u"blue");
        const QcSignTextMesher::Quad quad = cache.quads(pos, oak, light, *m_content, uv).front();
        QCOMPARE(cache.builds(), 2);
        QCOMPARE(quad[0].tint[0], blue.red);
        QCOMPARE(quad[0].tint[2], blue.blue);
        QVERIFY(blue.blue > black.blue);
    }

    void everyDyeHasASignColour()
    {
        join();
        int colours = 0;
        for (const QcItemDefinition &item : m_content->items()) {
            if (!item.dye.isEmpty()) {
                QVERIFY2(QcSignText::colorOfDye(item.dye), qPrintable(item.dye));
                ++colours;
            }
        }
        QCOMPARE(colours, 16);
        QCOMPARE(QcSignText::colorOfDye(u"black"), std::optional(QString()));
        QVERIFY(!QcSignText::colorOfDye(u"chartreuse"));
    }
};

QTEST_GUILESS_MAIN(TestSignColours)
#include "test_sign_colours.moc"

// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/entitymeshes/QcSignTextCache.h"
#include "clientcore/entitymeshes/QcSignTextMesher.h"
#include "clientcore/session/QcChunkExtras.h"
#include "clientcore/session/QcClientSignEditor.h"
#include "common/protocol/QcProtocol.h"
#include "QcPlayerUseFixture.h"
#include "server/storage/QcBlockEntityCodec.h"

using namespace Qt::StringLiterals;

// Signs (decision E-766): placing one turns it towards the player (16 steps) or hangs it on the
// clicked side and opens its editor; the player's lines are cleaned (control characters, length,
// lines of the block) and kept only for the sign whose editor the server opened, within reach and
// the rate limit; every client with the chunk learns the text, it goes and comes with the chunk
// and the save, and breaking the sign drops the sign item and clears the text.
class TestSigns : public QcPlayerUseFixture
{
    Q_OBJECT

private:
    using Lines = QcSignText::Lines;

    // Places a held oak sign on the top of the block below `cell` while looking along `yaw`.
    void placeOnFloor(QcBlockPos cell, float yaw)
    {
        face(yaw);
        send(QcMsg::UseItem{cell.offset(0, -1, 0), QcBlockFace::Up});
    }

    const Lines *textAt(QcBlockPos pos) const { return m_server->world().signs().at(pos); }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void craftedFromPlanksAndAStick()
    {
        join();
        QCOMPARE(crafted({{0, u"oak_planks"_s}, {1, u"oak_planks"_s}, {2, u"oak_planks"_s}, {3, u"oak_planks"_s}, {4, u"oak_planks"_s},
                          {5, u"oak_planks"_s}, {7, u"stick"_s}},
                         3),
                 (std::pair{u"qtcraft:oak_sign"_s, 3}));
        QCOMPARE(crafted({{0, u"spruce_planks"_s}, {1, u"spruce_planks"_s}, {2, u"spruce_planks"_s}, {3, u"spruce_planks"_s},
                          {4, u"spruce_planks"_s}, {5, u"spruce_planks"_s}, {7, u"stick"_s}},
                         3),
                 (std::pair{u"qtcraft:spruce_sign"_s, 3}));
        QCOMPARE(crafted({{4, u"iron_ingot"_s}, {1, u"signal_dust_chunk"_s}, {3, u"signal_dust_chunk"_s}, {5, u"signal_dust_chunk"_s},
                          {7, u"signal_dust_chunk"_s}},
                         3),
                 (std::pair{u"qtcraft:compass"_s, 1}));
        QCOMPARE(crafted({{4, u"gold_ingot"_s}, {1, u"signal_dust_chunk"_s}, {3, u"signal_dust_chunk"_s}, {5, u"signal_dust_chunk"_s},
                          {7, u"signal_dust_chunk"_s}},
                         3),
                 (std::pair{u"qtcraft:clock"_s, 1}));
    }

    void standsTurnedTowardsThePlayerAndOpensItsEditor()
    {
        join();
        hold(u"oak_sign"_s, 4);
        const QcBlockPos north = m_feet.offset(0, 0, -2);
        placeOnFloor(north, 0.0f);
        QCOMPARE(blockNameAt(north), u"qtcraft:oak_sign"_s);
        QCOMPARE(propertyAt(north, u"attach"_s), u"floor"_s);
        QCOMPARE(propertyAt(north, u"rotation"_s), u"0"_s);
        QCOMPARE(m_client->signEditors.size(), 1);
        QCOMPARE(m_client->signEditors.last().pos, north);
        QVERIFY(QcSignText::isEmpty(m_client->signEditors.last().lines));

        // Looking south-east by a little more than half a step: 135° / 22.5° = 6 sixteenths.
        const QcBlockPos other = m_feet.offset(2, 0, 2);
        placeOnFloor(other, 136.0f);
        QCOMPARE(propertyAt(other, u"rotation"_s), u"6"_s);
        QCOMPARE(m_client->signEditors.last().pos, other);
        QCOMPARE(countOf(baseItem(u"oak_sign"_s)), 2);
    }

    void hangsOnTheClickedSide()
    {
        join();
        const QcBlockPos wall = m_feet.offset(2, 0, 0);
        m_server->world().setBlock(wall, id("qtcraft:stone"));
        hold(u"birch_sign"_s);
        face(90.0f);
        send(QcMsg::UseItem{wall, QcBlockFace::West, {0.0, 0.5, 0.5}});
        const QcBlockPos sign = wall.offset(-1, 0, 0);
        QCOMPARE(blockNameAt(sign), u"qtcraft:birch_sign"_s);
        QCOMPARE(propertyAt(sign, u"attach"_s), u"east"_s);
        QCOMPARE(propertyAt(sign, u"rotation"_s), u"0"_s);

        // The text faces away from the wall, towards the player (west).
        const std::optional<QcSignTextMesher::Face> face = QcSignTextMesher::faceOf(*m_content, *m_server->world().blockAt(sign), sign);
        QVERIFY(face);
        QVERIFY(face->normal.x < -0.99);
        QVERIFY(face->centre.x < sign.x + 1.0 - 0.1 && face->centre.x > sign.x + 0.8);
    }

    void writtenTextIsCleanedAndSentToEveryone()
    {
        join();
        hold(u"oak_sign"_s);
        const QcBlockPos sign = m_feet.offset(0, 0, -2);
        placeOnFloor(sign, 0.0f);
        m_client->signTexts.clear();

        const Lines written{u"  Welcome\u0007 home  "_s, u"x"_s.repeated(40), u"\u202Eevil"_s, u"Last"_s};
        send(QcMsg::EditSign{sign, written});
        ticks(1);
        const Lines expected{u"Welcome home"_s, u"x"_s.repeated(15), u"evil"_s, u"Last"_s};
        QVERIFY(textAt(sign));
        QCOMPARE(*textAt(sign), expected);
        QCOMPARE(m_client->signTexts.size(), 1);
        QCOMPARE(m_client->signTexts.last().pos, sign);
        QCOMPARE(m_client->signTexts.last().lines, expected);

        // The editor was used up: a second edit without opening it again changes nothing, and the
        // client gets the true text back.
        send(QcMsg::EditSign{sign, Lines{u"Hacked"_s}});
        ticks(1);
        QCOMPARE(*textAt(sign), expected);
        QCOMPARE(m_client->signTexts.last().lines, expected);

        // Using the sign opens the editor with its text; sneaking does not.
        send(QcMsg::UseItem{sign, QcBlockFace::South});
        QCOMPARE(m_client->signEditors.last().lines, expected);
        send(QcMsg::EditSign{sign, Lines{u"New"_s}});
        ticks(1);
        QCOMPARE(*textAt(sign), (Lines{u"New"_s}));
    }

    // A player who dies with the editor open loses what he typed (decision E-928): the client
    // sends the draft as the screen closes, the server refuses edits from the dead and sends the
    // sign's true text back.
    void deathLosesTheTypedText()
    {
        join();
        hold(u"oak_sign"_s);
        const QcBlockPos sign = m_feet.offset(0, 0, -2);
        placeOnFloor(sign, 0.0f);
        send(QcMsg::EditSign{sign, Lines{u"Before"_s}});
        ticks(1);
        send(QcMsg::UseItem{sign, QcBlockFace::South});
        QCOMPARE(m_client->signEditors.last().lines, (Lines{u"Before"_s}));
        m_client->signTexts.clear();

        chat(u"/kill"_s);
        ticks(2);
        send(QcMsg::EditSign{sign, Lines{u"Typed"_s}});
        ticks(1);
        QVERIFY(textAt(sign));
        QCOMPARE(*textAt(sign), (Lines{u"Before"_s}));
        QVERIFY(!m_client->signTexts.isEmpty());
        QCOMPARE(m_client->signTexts.last().pos, sign);
        QCOMPARE(m_client->signTexts.last().lines, (Lines{u"Before"_s}));
    }

    void refusesOtherBlocksDistantSignsAndFloods()
    {
        join();
        hold(u"oak_sign"_s);
        const QcBlockPos sign = m_feet.offset(0, 0, -2);
        placeOnFloor(sign, 0.0f);
        // Not the opened sign: a stone.
        const QcBlockPos stone = m_feet.offset(1, 0, -2);
        m_server->world().setBlock(stone, id("qtcraft:stone"));
        send(QcMsg::EditSign{stone, Lines{u"Stone"_s}});
        QVERIFY(!m_server->world().signs().contains(stone));

        // A sign out of reach opens nothing.
        const QcBlockPos far = m_feet.offset(0, 0, -30);
        m_server->world().setBlock(far, id("qtcraft:oak_sign"));
        const qsizetype editors = m_client->signEditors.size();
        send(QcMsg::UseItem{far, QcBlockFace::South});
        QCOMPARE(m_client->signEditors.size(), editors);
        send(QcMsg::EditSign{far, Lines{u"Far"_s}});
        QVERIFY(!textAt(far));

        // Edits beyond the Sign bucket (burst 4, two tokens spent above, one a second) are dropped.
        int kept = 0;
        for (int round = 0; round < 8; ++round) {
            send(QcMsg::UseItem{sign, QcBlockFace::South});
            send(QcMsg::EditSign{sign, Lines{QString::number(round)}});
            kept += textAt(sign) && (*textAt(sign))[0] == QString::number(round) ? 1 : 0;
        }
        QVERIFY2(kept >= 2 && kept <= 4, qPrintable(QString::number(kept)));
    }

    void breakingDropsTheSignAndClearsTheText()
    {
        join();
        hold(u"spruce_sign"_s);
        const QcBlockPos sign = m_feet.offset(0, 0, -2);
        placeOnFloor(sign, 0.0f);
        send(QcMsg::EditSign{sign, Lines{u"Bye"_s}});
        QVERIFY(textAt(sign));
        m_client->signTexts.clear();

        send(QcMsg::SelectHotbarSlot{8});
        mine(sign);
        ticks(2);
        QVERIFY(!textAt(sign));
        QCOMPARE(droppedCount(baseItem(u"spruce_sign"_s)), 1);
        QCOMPARE(m_client->signTexts.size(), 1);
        QVERIFY(QcSignText::isEmpty(m_client->signTexts.last().lines));
    }

    void textIsSavedAndComesBackWithItsChunk()
    {
        QcBlockEntityRecord record;
        record.chunk = {0, 0};
        record.signs = {{{3, 20, 5}, Lines{u"One"_s, {}, u"Three"_s, {}}, {}}, {{4, 21, 5}, Lines{u"Ä€°"_s}, {}}};
        const QByteArray blob = QcBlockEntityCodec::encode(record, *m_content);
        QcLoadWarnings warnings;
        const QcResult<QcBlockEntityRecord> read = QcBlockEntityCodec::decode(blob, {0, 0}, *m_content, warnings);
        QVERIFY2(read.has_value(), qPrintable(read ? QString() : read.error()));
        QCOMPARE(read->signs, record.signs);
        // A damaged sign (a line longer than any save holds) damages the chunk's whole record.
        QcBlockEntityRecord damaged = record;
        damaged.signs[1].lines[0] = u"x"_s.repeated(300);
        QVERIFY(!QcBlockEntityCodec::decode(QcBlockEntityCodec::encode(damaged, *m_content), {0, 0}, *m_content, warnings));

        m_unloadChunks = true;
        join();
        const QcBlockPos far = m_feet.offset(160, 0, 0);
        m_server->world().loadChunk(far.chunk());
        m_server->world().setBlock(far, id("qtcraft:oak_sign"));
        m_server->world().signs().set(far, Lines{u"Kept"_s, u"x"_s.repeated(20)});
        ticks(200);
        QVERIFY(!m_server->world().chunk(far.chunk()));
        m_server->world().loadChunk(far.chunk());
        QVERIFY(textAt(far));
        QCOMPARE((*textAt(far))[0], u"Kept"_s);
        // Loaded lines are cleaned again to the block's own line length.
        QCOMPARE((*textAt(far))[1], u"x"_s.repeated(15));
    }

    void protocolAndClientMirror()
    {
        const QcMsg::SignText text{{17, 30, -5}, Lines{u"a"_s, u"b"_s}, {}};
        const QcResult<QcMessage> decoded = QcProtocol::decode(QcProtocol::encode(QcMessage(text)), QcProtocol::Peer::Server);
        QVERIFY(decoded && std::holds_alternative<QcMsg::SignText>(*decoded));
        QCOMPARE(std::get<QcMsg::SignText>(*decoded).lines, text.lines);
        // Lines longer than the wire allows are refused; the client may not send SignText.
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMessage(QcMsg::SignText{{1, 2, 3}, Lines{u"x"_s.repeated(300)}, {}})), QcProtocol::Peer::Server));
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMessage(text)), QcProtocol::Peer::Client));
        QVERIFY(QcProtocol::decode(QcProtocol::encode(QcMessage(QcMsg::EditSign{{1, 2, 3}, Lines{u"hi"_s}})), QcProtocol::Peer::Client));

        QcChunkExtras extras;
        extras.setSign(text);
        extras.setSign({{40, 30, 40}, Lines{u"c"_s}, {}});
        QCOMPARE(extras.signs().size(), 2);
        extras.setSign({{40, 30, 40}, Lines{}, {}});
        QCOMPARE(extras.signs().size(), 1);
        const quint64 generation = extras.signGeneration();
        extras.chunkArrived(text.pos.chunk());
        QVERIFY(extras.signs().isEmpty());
        QVERIFY(extras.signGeneration() > generation);
    }

    // The editor keeps the lines as they are typed (docs/UI-Leitfaden.md 5, pattern 1): closing it
    // in any way sends them; edits while no editor is open change nothing.
    void editorSendsTheLinesAsTyped()
    {
        QcClientSignEditor editor;
        QList<QcMessage> sent;
        editor.setMessageSender([&sent](const QcMessage &message) { sent.append(message); });
        editor.setLines(Lines{u"ignored"_s});
        QVERIFY(editor.lines()[0].isEmpty());

        QVERIFY(editor.handle(QcMessage(QcMsg::OpenSignEditor{{3, 64, -2}, Lines{u"old"_s, u"text"_s}})));
        QVERIFY(editor.isOpen());
        QCOMPARE(editor.lines()[0], u"old"_s);
        editor.setLines(Lines{u"new"_s, u"text"_s});
        QVERIFY(editor.write(editor.lines()));
        QVERIFY(!editor.isOpen());
        QCOMPARE(sent.size(), 1);
        QVERIFY(std::holds_alternative<QcMsg::EditSign>(sent[0]));
        QCOMPARE(std::get<QcMsg::EditSign>(sent[0]).lines[0], u"new"_s);
        QCOMPARE(std::get<QcMsg::EditSign>(sent[0]).pos, (QcBlockPos{3, 64, -2}));

        // Closed: a late edit is dropped and nothing more is sent.
        editor.setLines(Lines{u"late"_s});
        QCOMPARE(editor.lines()[0], u"new"_s);
        QVERIFY(!editor.write(editor.lines()));
        QCOMPARE(sent.size(), 1);
    }

    void textQuadsAreKeptUntilTheSignChanges()
    {
        buildArea();
        QcSignTextCache cache;
        const QcEntityMesher::UvLookup uv = [](const QString &) { return QRectF(0.0, 0.0, 1.0, 1.0); };
        const QcBlockPos near{1, 64, 1};
        QHash<QcBlockPos, QcSignText::Text> signs{{near, {Lines{u"Hi"_s}, {}}}, {{200, 64, 200}, {Lines{u"Far"_s}, {}}}};
        cache.setSigns(signs, 1);
        QCOMPARE(cache.signsNear({0.0, 64.0, 0.0}, 32.0), QList<QcBlockPos>{near});

        // Built once, kept while block and light stay (light differences below a step included).
        const QcBlockId oak = id("qtcraft:oak_sign");
        const QcEntityLight light(1.0f, 0.0f);
        QCOMPARE(cache.quads(near, oak, light, *m_content, uv).size(), std::size_t(2));
        QCOMPARE(cache.quads(near, oak, QcEntityLight(1.0f, 0.001f), *m_content, uv).size(), std::size_t(2));
        QCOMPARE(cache.builds(), 1);
        QCOMPARE(cache.quads(near, oak, QcEntityLight(0.5f, 0.0f), *m_content, uv).size(), std::size_t(2));
        QCOMPARE(cache.builds(), 2);
        const QcBlockId turned = m_content->withProperty(oak, u"rotation"_s, u"4"_s);
        QCOMPARE(cache.quads(near, turned, light, *m_content, uv).size(), std::size_t(2));
        QCOMPARE(cache.builds(), 3);

        // A new text counts only with a new generation.
        signs[near].lines = Lines{u"Bye"_s};
        cache.setSigns(signs, 1);
        QCOMPARE(cache.quads(near, turned, light, *m_content, uv).size(), std::size_t(2));
        cache.setSigns(signs, 2);
        QCOMPARE(cache.quads(near, turned, light, *m_content, uv).size(), std::size_t(3));
        QCOMPARE(cache.builds(), 4);

        // A block that is no sign shows no text; an unknown position has none.
        QVERIFY(cache.quads(near, id("qtcraft:stone"), light, *m_content, uv).empty());
        QVERIFY(cache.quads({5, 5, 5}, oak, light, *m_content, uv).empty());
        cache.clear();
        QVERIFY(cache.signsNear({0.0, 64.0, 0.0}, 32.0).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestSigns)
#include "test_signs.moc"

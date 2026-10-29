// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fixtures/QcPlayerUseFixture.h"
#include "server/signal/QcNoteBudget.h"

#include <cmath>

using namespace Qt::StringLiterals;

// The note block (decision E-767): using it raises its note by a semitone and plays it, a rising
// signal plays it once, the block below picks the instrument, a block on top silences it. Sounds are
// read as the player's client hears them (key and pitch).
class TestNoteBlock : public QcPlayerUseFixture
{
    Q_OBJECT

private:
    QcBlockPos m_note;

    // A note block two blocks east of the player on `below`, at `note`.
    void placeNote(const QString &below, int note = 0)
    {
        m_note = m_feet.offset(2, 0, 0);
        m_server->world().setBlock(m_note.offset(0, -1, 0), id(qPrintable(u"qtcraft:"_s + below)));
        putAt(m_note, m_content->withProperty(id("qtcraft:note_block"), u"note"_s, QString::number(note)));
        ticks(2);
    }

    void use()
    {
        send(QcMsg::UseItem{m_note, QcBlockFace::West});
        // The switch budget refills one use every four ticks.
        ticks(5);
    }

    qsizetype notesHeard() const
    {
        return std::ranges::count_if(m_client->sounds, [](const QString &key) { return key.startsWith(u"qtcraft:note."_s); });
    }
    QString lastNote() const
    {
        for (qsizetype i = m_client->sounds.size() - 1; i >= 0; --i) {
            if (m_client->sounds[i].startsWith(u"qtcraft:note."_s)) {
                return m_client->sounds[i];
            }
        }
        return {};
    }
    float lastPitch() const
    {
        for (qsizetype i = m_client->sounds.size() - 1; i >= 0; --i) {
            if (m_client->sounds[i].startsWith(u"qtcraft:note."_s)) {
                return m_client->soundPitches[i];
            }
        }
        return 0.0f;
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void craftedFromPlanksAndAChunk()
    {
        buildArea();
        QList<std::pair<int, QString>> grid;
        for (int cell = 0; cell < 9; ++cell) {
            grid.append({cell, cell == 4 ? u"signal_dust_chunk"_s : (cell % 2 ? u"birch_planks"_s : u"oak_planks"_s)});
        }
        QCOMPARE(crafted(grid, 3), (std::pair{u"qtcraft:note_block"_s, 1}));
        grid[4].second = u"signal_dust"_s;
        QCOMPARE(crafted(grid, 3), (std::pair<QString, int>{}));
    }

    void usingRaisesTheNoteAndPlaysIt()
    {
        join();
        placeNote(u"oak_planks"_s);
        use();
        QCOMPARE(propertyAt(m_note, u"note"_s), u"1"_s);
        QCOMPARE(lastNote(), u"qtcraft:note.bass"_s);
        QCOMPARE(lastPitch(), float(std::exp2(-11.0 / 12.0)));
        use();
        QCOMPARE(propertyAt(m_note, u"note"_s), u"2"_s);
        QCOMPARE(notesHeard(), 2);

        // From the highest note back to the lowest: half the instrument's pitch.
        putAt(m_note, m_content->withProperty(id("qtcraft:note_block"), u"note"_s, u"23"_s));
        use();
        QCOMPARE(lastPitch(), 2.0f);
        use();
        QCOMPARE(propertyAt(m_note, u"note"_s), u"0"_s);
        QCOMPARE(lastPitch(), 0.5f);
    }

    void instrumentsFromTheBlockBelow()
    {
        join();
        const QList<std::pair<QString, QString>> instruments{
            {u"stone"_s, u"basedrum"_s},  {u"sand"_s, u"snare"_s},          {u"glass"_s, u"hat"_s},
            {u"white_wool"_s, u"guitar"_s}, {u"gold_block"_s, u"bell"_s},     {u"clay"_s, u"flute"_s},
            {u"packed_ice"_s, u"chime"_s}, {u"bone_block"_s, u"xylophone"_s}, {u"iron_block"_s, u"iron_xylophone"_s},
            {u"dirt"_s, u"harp"_s}};
        for (const auto &[below, instrument] : instruments) {
            placeNote(below);
            use();
            QCOMPARE(lastNote(), u"qtcraft:note."_s + instrument);
            QVERIFY2(m_content->assets().sound(u"qtcraft:note."_s + instrument), qPrintable(instrument));
        }
        QVERIFY(m_content->assets().sound(u"qtcraft:note.bass"_s));
    }

    void aRisingSignalPlaysOnce()
    {
        join();
        placeNote(u"stone"_s, 12);
        const QcBlockPos lever = m_note.offset(0, 0, 1);
        putAt(lever, id("qtcraft:lever"));
        ticks(3);
        QCOMPARE(notesHeard(), 0);

        QVERIFY(m_server->systems().circuits.use(lever));
        ticks(3);
        QCOMPARE(notesHeard(), 1);
        QCOMPARE(lastPitch(), 1.0f);
        QCOMPARE(propertyAt(m_note, u"powered"_s), u"true"_s);
        // Staying powered plays nothing more; the note does not change.
        ticks(20);
        QCOMPARE(notesHeard(), 1);
        QCOMPARE(propertyAt(m_note, u"note"_s), u"12"_s);

        QVERIFY(m_server->systems().circuits.use(lever));
        ticks(3);
        QCOMPARE(propertyAt(m_note, u"powered"_s), u"false"_s);
        QCOMPARE(notesHeard(), 1);
        QVERIFY(m_server->systems().circuits.use(lever));
        ticks(3);
        QCOMPARE(notesHeard(), 2);
    }

    void aBlockOnTopSilencesIt()
    {
        join();
        placeNote(u"stone"_s);
        m_server->world().setBlock(m_note.offset(0, 1, 0), id("qtcraft:stone"));
        use();
        QCOMPARE(propertyAt(m_note, u"note"_s), u"1"_s);
        QCOMPARE(notesHeard(), 0);
        m_server->world().setBlock(m_note.offset(0, 1, 0), qcAirBlockId);
        use();
        QCOMPARE(notesHeard(), 1);
    }

    void notesAreBudgeted()
    {
        // One block plays at most once every minIntervalTicks, all blocks together at most
        // maxPerTick notes a tick.
        QcNoteBudget budget;
        constexpr quint64 start = 10;
        QVERIFY(budget.allow({0, 0, 0}, start));
        QVERIFY(!budget.allow({0, 0, 0}, start));
        QVERIFY(!budget.allow({0, 0, 0}, start + QcNoteBudget::minIntervalTicks - 1));
        QVERIFY(budget.allow({0, 0, 0}, start + QcNoteBudget::minIntervalTicks));
        int allowed = 0;
        for (int x = 1; x <= 3 * QcNoteBudget::maxPerTick; ++x) {
            allowed += budget.allow({x, 0, 0}, start + 10) ? 1 : 0;
        }
        QCOMPARE(allowed, QcNoteBudget::maxPerTick);
        // A new tick starts a new count and forgets blocks that played long enough ago.
        QVERIFY(budget.allow({0, 0, 0}, start + 11));
        QCOMPARE(budget.remembered(), qsizetype(QcNoteBudget::maxPerTick + 1));

        // In the world: notes of one block in one tick sound once.
        join();
        placeNote(u"stone"_s);
        m_server->systems().circuits.playNote(m_note);
        m_server->systems().circuits.playNote(m_note);
        ticks(1);
        QCOMPARE(notesHeard(), 1);
    }
};

QTEST_GUILESS_MAIN(TestNoteBlock)
#include "test_note_block.moc"

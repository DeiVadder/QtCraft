// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcClientBossBars.h"
#include "common/content/mobs/QcBossBarDefinition.h"
#include "common/content/mobs/QcMobSerializer.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/protocol/QcProtocol.h"
#include "fixtures/QcTestUtil.h"

#include <QSignalSpy>
#include <QTest>

#include <limits>

using namespace Qt::StringLiterals;

namespace {

// A bar of the entity `entity` filled to `progress`.
QcMsg::BossBar barOf(quint32 entity, float progress = 0.5f, bool shown = true)
{
    return {.entity = entity,
            .kind = u"qtcraft:charred_hydra"_s,
            .name = QString(),
            .progress = progress,
            .colour = static_cast<quint8>(QcBossBarColour::Purple),
            .shown = shown};
}

// Whether `bar` decodes after encoding, as the client receives it.
bool decodes(const QcMsg::BossBar &bar)
{
    return QcProtocol::decode(QcProtocol::encode(QcMessage(bar)), QcProtocol::Peer::Server).has_value();
}

} // namespace

// The general boss bar (decision E-1127): its message through the codec, with the values the
// decoder refuses; the client's bars as the messages come, change, go and a change of dimension
// takes them; and the armour parts of a model (decision E-1126) through the content sync.
class TestBossBars : public QObject
{
    Q_OBJECT

private slots:
    void theMessageRoundTrips()
    {
        QcMsg::BossBar bar = barOf(42, 0.25f, false);
        bar.name = u"Grim"_s;
        bar.colour = static_cast<quint8>(QcBossBarColour::White);
        const QcResult<QcMessage> decoded = QcProtocol::decode(QcProtocol::encode(QcMessage(bar)), QcProtocol::Peer::Server);
        QVERIFY(decoded && std::holds_alternative<QcMsg::BossBar>(*decoded));
        const auto &copy = std::get<QcMsg::BossBar>(*decoded);
        QCOMPARE(copy.entity, 42u);
        QCOMPARE(copy.kind, bar.kind);
        QCOMPARE(copy.name, u"Grim"_s);
        QCOMPARE(copy.progress, 0.25f);
        QCOMPARE(copy.colour, static_cast<quint8>(QcBossBarColour::White));
        QVERIFY(!copy.shown);
    }

    void theDecoderRefusesWhatCannotBe_data()
    {
        QTest::addColumn<quint32>("entity");
        QTest::addColumn<float>("progress");
        QTest::addColumn<int>("colour");
        QTest::addColumn<int>("nameLength");
        QTest::addColumn<bool>("valid");
        const int purple = static_cast<int>(QcBossBarColour::Purple);
        QTest::newRow("full") << 1u << 1.0f << purple << 0 << true;
        QTest::newRow("empty") << 1u << 0.0f << purple << 0 << true;
        QTest::newRow("last colour") << 1u << 0.5f << qcBossBarColours - 1 << 0 << true;
        QTest::newRow("longest name") << 1u << 0.5f << purple << 256 << true;
        QTest::newRow("no entity") << 0u << 0.5f << purple << 0 << false;
        QTest::newRow("progress below 0") << 1u << -0.01f << purple << 0 << false;
        QTest::newRow("progress above 1") << 1u << 1.01f << purple << 0 << false;
        QTest::newRow("progress not a number") << 1u << std::numeric_limits<float>::quiet_NaN() << purple << 0 << false;
        QTest::newRow("unknown colour") << 1u << 0.5f << qcBossBarColours << 0 << false;
        QTest::newRow("name too long") << 1u << 0.5f << purple << 257 << false;
    }

    void theDecoderRefusesWhatCannotBe()
    {
        QFETCH(quint32, entity);
        QFETCH(float, progress);
        QFETCH(int, colour);
        QFETCH(int, nameLength);
        QFETCH(bool, valid);
        QcMsg::BossBar bar = barOf(entity, progress);
        bar.colour = static_cast<quint8>(colour);
        bar.name = QString(nameLength, u'x');
        QCOMPARE(decodes(bar), valid);
    }

    // The client keeps the bars in the order they came: a change stays in place, a bar that goes
    // leaves the others, at most maxShown show, and a change of dimension takes them all.
    void theClientFollowsTheMessages()
    {
        QcClientBossBars bars;
        QSignalSpy changed(&bars, &QcClientBossBars::changed);
        for (quint32 entity = 1; entity <= 5; ++entity) {
            QVERIFY(bars.handle(barOf(entity)));
        }
        QCOMPARE(changed.count(), 5);
        QCOMPARE(bars.bars().size(), QcClientBossBars::maxShown);
        QCOMPARE(bars.bars().constFirst().entity, 1u);

        QVERIFY(bars.handle(barOf(2, 0.1f)));
        QCOMPARE(bars.bars().at(1).progress, 0.1f);
        QVERIFY(bars.handle(barOf(1, 0.5f, false)));
        QCOMPARE(bars.bars().size(), 4);
        QCOMPARE(bars.bars().constFirst().entity, 2u);
        QCOMPARE(bars.bars().constLast().entity, 5u);

        // A bar going that was never there changes nothing.
        const qsizetype before = changed.count();
        QVERIFY(bars.handle(barOf(99, 0.5f, false)));
        QCOMPARE(changed.count(), before);

        // The others still see the change of dimension.
        QVERIFY(!bars.handle(QcMsg::DimensionChange{}));
        QVERIFY(bars.bars().isEmpty());
        QCOMPARE(changed.count(), before + 1);
    }

    // The armour parts of the hydra's model keep their mark through the content sync.
    void armourPartsGoThroughTheContentSync()
    {
        const std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
        const QcMobDefinition &hydra = content->mobs()[*content->mobKind(u"qtcraft:charred_hydra"_s)];
        const qsizetype armoured = std::ranges::count(hydra.parts, true, &QcMobPart::armour);
        QVERIFY(armoured > 0 && armoured < hydra.parts.size());

        QByteArray bytes;
        {
            QDataStream out(&bytes, QIODevice::WriteOnly);
            QcMobSerializer::writeMob(out, hydra);
        }
        QDataStream in(bytes);
        const std::optional<QcMobDefinition> back = QcMobSerializer::readMob(in);
        QVERIFY(back);
        QCOMPARE(back->parts.size(), hydra.parts.size());
        for (qsizetype index = 0; index < hydra.parts.size(); ++index) {
            QCOMPARE(back->parts.at(index).armour, hydra.parts.at(index).armour);
        }
    }
};

QTEST_GUILESS_MAIN(TestBossBars)
#include "test_boss_bars.moc"

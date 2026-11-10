// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// The atmosphere of biomes and the particles of blocks (decisions E-1120, E-1121): parsing, the
// wire format and the values of the nether biomes and blocks in the base content.

#include "common/content/loader/QcBaseContent.h"
#include "common/content/registry/QcJsonFieldReader.h"
#include "common/content/worldgen/QcAtmosphereParser.h"
#include "common/protocol/QcProtocol.h"

#include <QJsonDocument>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

QJsonObject object(const char *json)
{
    return QJsonDocument::fromJson(QByteArray(json)).object();
}

struct Parsed
{
    QcBiomeAtmosphere atmosphere;
    QString error;
};

Parsed parse(const char *json)
{
    const QJsonObject value = object(json);
    QcJsonFieldReader reader{value, u"biome 'test'"_s, {}};
    QcBiomeAtmosphere atmosphere = QcAtmosphereParser::parseAtmosphere(value, reader, u"test"_s);
    return {atmosphere, reader.error};
}

struct ParsedEmitters
{
    QList<QcParticleEmitter> emitters;
    QString error;
};

ParsedEmitters parseEmitters(const char *json)
{
    const QJsonObject value = object(json);
    QcJsonFieldReader reader{value, u"block 'test'"_s, {}};
    QList<QcParticleEmitter> emitters = QcAtmosphereParser::parseEmitters(value.value(u"particles"_s), reader);
    return {emitters, reader.error};
}

template<typename T>
std::optional<T> roundTrip(const T &message)
{
    const QcResult<QcMessage> decoded = QcProtocol::decode(QcProtocol::encode(QcMessage{message}), QcProtocol::Peer::Server);
    if (!decoded || !std::holds_alternative<T>(*decoded)) {
        return std::nullopt;
    }
    return std::get<T>(*decoded);
}

} // namespace

class TestAtmosphereContent : public QObject
{
    Q_OBJECT

    std::shared_ptr<const QcContentRegistry> m_base;

    const QcBiomeDefinition &biome(const QString &name) const
    {
        const QcBiomeDefinition *found = m_base->worldgen().biome(name);
        if (!found) {
            qFatal("biome missing");
        }
        return *found;
    }

    QList<QcParticleEmitter> particlesOf(const QString &block) const
    {
        return m_base->block(m_base->blockId(block).value_or(qcAirBlockId)).particles;
    }

private slots:
    void initTestCase()
    {
        const QcResult<QcBaseContent::Loaded> base = QcBaseContent::load();
        QVERIFY(base);
        m_base = base->registry;
    }

    // Every field of the atmosphere object reaches its member; sound names get the pack's namespace.
    void parsesEveryField()
    {
        const Parsed parsed = parse(R"({"fog": {"colour": "#330808", "start": 0.05, "end": 0.5},)"
                                    R"("particles": [{"kind": "ash", "chance": 0.25}, {"kind": "warped_spore", "chance": 0.5}],)"
                                    R"("ambience": {"loop": "wind.loop", "additions": "other:moan", "additionsChance": 0.0111},)"
                                    R"("music": {"scale": "minor", "lead": "saw", "lowestRoot": 36, "highestRoot": 40,)"
                                    R"("slowestTempo": 50, "fastestTempo": 60, "leadLowpass": 1200, "drone": true}})");
        QCOMPARE(parsed.error, QString());
        const QcBiomeAtmosphere &atmosphere = parsed.atmosphere;
        QCOMPARE(atmosphere.fogColour, std::optional<quint32>(0x330808));
        QCOMPARE(atmosphere.fogStart, std::optional<double>(0.05));
        QCOMPARE(atmosphere.fogEnd, std::optional<double>(0.5));
        const QList<QcParticleChance> particles = {{QcParticleKind::Ash, 0.25}, {QcParticleKind::WarpedSpore, 0.5}};
        QCOMPARE(atmosphere.particles, particles);
        QCOMPARE(atmosphere.loop, u"test:wind.loop"_s);
        QCOMPARE(atmosphere.additions, u"other:moan"_s);
        QCOMPARE(atmosphere.additionsChance, 0.0111);

        QVERIFY(atmosphere.music);
        QCOMPARE(atmosphere.music->scale, QcMusicStyle::Scale::Minor);
        QCOMPARE(atmosphere.music->lead, QcSoundWave::Saw);
        QCOMPARE(atmosphere.music->lowestRoot, 36);
        QCOMPARE(atmosphere.music->highestRoot, 40);
        QCOMPARE(atmosphere.music->slowestTempo, 50);
        QCOMPARE(atmosphere.music->fastestTempo, 60);
        QCOMPARE(atmosphere.music->leadLowpass, 1200.0);
        QVERIFY(atmosphere.music->drone);
    }

    // Without fields the atmosphere changes nothing; a fog of colour only keeps the distances.
    void missingFieldsKeepTheDimension()
    {
        const Parsed empty = parse("{}");
        QCOMPARE(empty.error, QString());
        QVERIFY(empty.atmosphere.isEmpty());

        const Parsed colour = parse("{\"fog\": {\"colour\": \"#102030\"}}");
        QCOMPARE(colour.error, QString());
        QCOMPARE(colour.atmosphere.fogColour, std::optional<quint32>(0x102030));
        QVERIFY(!colour.atmosphere.fogStart);
        QVERIFY(!colour.atmosphere.fogEnd);
        QVERIFY(!colour.atmosphere.music);
    }

    void rejectsBadAtmospheres_data()
    {
        QTest::addColumn<QString>("json");
        QTest::addColumn<QString>("expected");
        QTest::newRow("unknown particle") << QString::fromUtf8(R"({"particles": [{"kind": "smoke", "chance": 0.1}]})") << u"unknown particle"_s;
        QTest::newRow("chance above 1") << QString::fromUtf8(R"({"particles": [{"kind": "ash", "chance": 1.5}]})") << u"chance"_s;
        QTest::newRow("five particles") << QString::fromUtf8(R"({"particles": [{"kind": "ash"}, {"kind": "ash"}, {"kind": "ash"},)"
                                                             R"({"kind": "ash"}, {"kind": "ash"}]})")
                                        << u"at most 4"_s;
        QTest::newRow("particles no array") << QString::fromUtf8(R"({"particles": {"kind": "ash"}})") << u"array"_s;
        QTest::newRow("fog start after end") << QString::fromUtf8(R"({"fog": {"start": 0.6, "end": 0.5}})") << u"before its 'end'"_s;
        QTest::newRow("fog no colour") << QString::fromUtf8(R"({"fog": {"colour": "red-ish"}})") << u"colour"_s;
        QTest::newRow("unknown scale") << QString::fromUtf8(R"({"music": {"scale": "dorian"}})") << u"unknown scale"_s;
        QTest::newRow("unknown lead") << QString::fromUtf8(R"({"music": {"lead": "organ"}})") << u"lead 'organ'"_s;
        QTest::newRow("root too high") << QString::fromUtf8(R"({"music": {"lowestRoot": 80}})") << u"lowestRoot"_s;
        QTest::newRow("roots crossed") << QString::fromUtf8(R"({"music": {"lowestRoot": 50, "highestRoot": 40}})") << u"highestRoot"_s;
        QTest::newRow("tempos crossed") << QString::fromUtf8(R"({"music": {"slowestTempo": 90, "fastestTempo": 60}})") << u"fastestTempo"_s;
        QTest::newRow("lowpass too low") << QString::fromUtf8(R"({"music": {"leadLowpass": 50}})") << u"leadLowpass"_s;
        QTest::newRow("additions chance") << QString::fromUtf8(R"({"ambience": {"additions": "a", "additionsChance": 2}})") << u"additionsChance"_s;
    }

    // Problems fail the reader and name the field.
    void rejectsBadAtmospheres()
    {
        QFETCH(QString, json);
        QFETCH(QString, expected);
        const Parsed parsed = parse(json.toUtf8().constData());
        QVERIFY2(parsed.error.contains(expected), qPrintable(parsed.error));
        QVERIFY(parsed.error.startsWith(u"biome 'test'"_s));
    }

    // A block's emitters: kind, chance and face, "bottom" when none is named.
    void parsesEmitters()
    {
        const ParsedEmitters parsed = parseEmitters(R"({"particles": [{"kind": "dripping_tear", "chance": 0.2, "from": "faces"},)"
                                                    R"({"kind": "dripping_lava", "chance": 0.1, "from": "through"},)"
                                                    R"({"kind": "soul", "chance": 0.002, "from": "top"}, {"kind": "ash", "chance": 0.5}]})");
        QCOMPARE(parsed.error, QString());
        using From = QcParticleEmitter::From;
        const QList<QcParticleEmitter> expected = {{QcParticleKind::DrippingTear, 0.2, From::Faces},
                                                   {QcParticleKind::DrippingLava, 0.1, From::Through},
                                                   {QcParticleKind::Soul, 0.002, From::Top},
                                                   {QcParticleKind::Ash, 0.5, From::Bottom}};
        QCOMPARE(parsed.emitters, expected);

        QVERIFY(parseEmitters("{}").emitters.isEmpty());
        QVERIFY(parseEmitters("{}").error.isEmpty());
        QVERIFY(parseEmitters(R"({"particles": [{"kind": "ash", "from": "side"}]})").error.contains(u"unknown 'from'"_s));
        QVERIFY(parseEmitters(R"({"particles": [3]})").error.contains(u"objects"_s));
        QVERIFY(parseEmitters(R"({"particles": [{"kind": "x"}]})").error.contains(u"unknown particle 'x'"_s));
    }

    // Kinds and faces by name and back; unknown names give none.
    void namesParticleKinds()
    {
        for (int i = 0; i < qcParticleKindCount; ++i) {
            const auto kind = static_cast<QcParticleKind>(i);
            QCOMPARE(QcAtmosphere::particleKind(QcAtmosphere::name(kind)), std::optional<QcParticleKind>(kind));
        }
        QCOMPARE(QcAtmosphere::particleKind(u"crimson_spore"), std::optional<QcParticleKind>(QcParticleKind::CrimsonSpore));
        QVERIFY(!QcAtmosphere::particleKind(u"Ash"));
        QCOMPARE(QcAtmosphere::emitterFrom(u"through"), std::optional<QcParticleEmitter::From>(QcParticleEmitter::From::Through));
        QVERIFY(!QcAtmosphere::emitterFrom(u""));
    }

    // The server's message carries every field of the atmosphere, and an empty one as empty.
    void atmosphereCrossesTheWire()
    {
        const QcBiomeAtmosphere &wastes = biome(u"qtcraft:basalt_deltas"_s).atmosphere;
        QVERIFY(!wastes.isEmpty());
        QCOMPARE(roundTrip(QcMsg::Atmosphere{wastes}), std::optional<QcMsg::Atmosphere>(QcMsg::Atmosphere{wastes}));
        QCOMPARE(roundTrip(QcMsg::Atmosphere{}), std::optional<QcMsg::Atmosphere>(QcMsg::Atmosphere{}));

        QcBiomeAtmosphere partial;
        partial.fogEnd = 0.4;
        partial.particles = {{QcParticleKind::Soul, 0.5}};
        QCOMPARE(roundTrip(QcMsg::Atmosphere{partial}), std::optional<QcMsg::Atmosphere>(QcMsg::Atmosphere{partial}));

        // Only the server sends it.
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::Atmosphere{partial}), QcProtocol::Peer::Client));
    }

    // Implausible atmospheres are refused when read.
    void refusesImplausibleAtmospheres()
    {
        QcBiomeAtmosphere tooMany;
        tooMany.particles.fill({QcParticleKind::Ash, 0.1}, QcBiomeAtmosphere::maxParticles + 1);
        QVERIFY(!roundTrip(QcMsg::Atmosphere{tooMany}));

        QcBiomeAtmosphere badChance;
        badChance.particles = {{QcParticleKind::Ash, 3.0}};
        QVERIFY(!roundTrip(QcMsg::Atmosphere{badChance}));

        QcBiomeAtmosphere crossed;
        crossed.fogStart = 0.8;
        crossed.fogEnd = 0.2;
        QVERIFY(!roundTrip(QcMsg::Atmosphere{crossed}));
        // A scale or wave the client does not know.
        QcBiomeAtmosphere unknownScale;
        unknownScale.music = QcMusicStyle{};
        unknownScale.music->scale = static_cast<QcMusicStyle::Scale>(7);
        QVERIFY(!roundTrip(QcMsg::Atmosphere{unknownScale}));
        QcBiomeAtmosphere unknownWave;
        unknownWave.music = QcMusicStyle{};
        unknownWave.music->lead = static_cast<QcSoundWave>(42);
        QVERIFY(!roundTrip(QcMsg::Atmosphere{unknownWave}));
    }

    // Unknown keys are ignored with a warning, so a misplaced field does not go unnoticed.
    void warnsAboutUnknownKeys()
    {
        QTest::ignoreMessage(QtWarningMsg, "biome 'test' has an unknown key 'fogg', ignored");
        QTest::ignoreMessage(QtWarningMsg, "biome 'test' fog has an unknown key 'colour2', ignored");
        QTest::ignoreMessage(QtWarningMsg, "biome 'test' particles has an unknown key 'chanse', ignored");
        QTest::ignoreMessage(QtWarningMsg, "biome 'test' music has an unknown key 'tempo', ignored");
        const Parsed parsed = parse(R"({"fogg": {}, "fog": {"colour2": "x"}, "particles": [{"kind": "ash", "chanse": 1}],)"
                                    R"( "music": {"tempo": 3}})");
        QVERIFY2(parsed.error.isEmpty(), qPrintable(parsed.error));

        QTest::ignoreMessage(QtWarningMsg, "block 'test' particles has an unknown key 'face', ignored");
        QVERIFY(parseEmitters(R"({"particles": [{"kind": "ash", "face": "top"}]})").error.isEmpty());
    }

    // The particles of the blocks reach the client with the content.
    void blockParticlesCrossTheWire()
    {
        const std::optional<QcMsg::ContentSync> copy = roundTrip(QcMsg::ContentSync{m_base});
        QVERIFY(copy);
        for (const QString &name : {u"qtcraft:crying_obsidian"_s, u"qtcraft:lava"_s, u"qtcraft:soul_sand"_s, u"qtcraft:stone"_s}) {
            const QcBlockId id = copy->content->blockId(name).value_or(qcAirBlockId);
            QCOMPARE(copy->content->block(id).particles, particlesOf(name));
        }
    }

    void netherBiomes_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<quint32>("fog");
        QTest::addColumn<QList<QcParticleChance>>("particles");
        // The role model's fog colours and ambient particles with their chances.
        QTest::newRow("wastes") << u"qtcraft:nether_wastes"_s << 0x330808u << QList<QcParticleChance>{};
        QTest::newRow("soul sand valley") << u"qtcraft:soul_sand_valley"_s << 0x1b4745u
                                          << QList<QcParticleChance>{{QcParticleKind::Ash, 0.00625}};
        QTest::newRow("crimson forest") << u"qtcraft:crimson_forest"_s << 0x330303u
                                        << QList<QcParticleChance>{{QcParticleKind::CrimsonSpore, 0.025}};
        QTest::newRow("warped forest") << u"qtcraft:warped_forest"_s << 0x1a051au
                                       << QList<QcParticleChance>{{QcParticleKind::WarpedSpore, 0.01428}};
        QTest::newRow("basalt deltas") << u"qtcraft:basalt_deltas"_s << 0x685f70u
                                       << QList<QcParticleChance>{{QcParticleKind::WhiteAsh, 0.118093334}};
    }

    // Each nether biome has its fog, particles, ambience and music.
    void netherBiomes()
    {
        QFETCH(QString, name);
        QFETCH(quint32, fog);
        QFETCH(QList<QcParticleChance>, particles);
        const QcBiomeAtmosphere &atmosphere = biome(name).atmosphere;
        QCOMPARE(atmosphere.fogColour, std::optional<quint32>(fog));
        QCOMPARE(atmosphere.fogStart, std::optional<double>(0.05));
        QCOMPARE(atmosphere.fogEnd, std::optional<double>(0.5));
        QCOMPARE(atmosphere.particles, particles);
        const QString plain = name.section(u':', 1);
        QCOMPARE(atmosphere.loop, u"qtcraft:ambient.%1.loop"_s.arg(plain));
        QCOMPARE(atmosphere.additions, u"qtcraft:ambient.%1.additions"_s.arg(plain));
        QCOMPARE(atmosphere.additionsChance, 0.0111);
        QVERIFY(atmosphere.music);
        QVERIFY(m_base->assets().sound(atmosphere.loop));
        QVERIFY(m_base->assets().sound(atmosphere.loop)->loop);
        QVERIFY(m_base->assets().sound(atmosphere.additions));
    }

    // Overworld biomes keep the dimension's sky, sounds and music.
    void overworldBiomesHaveNoAtmosphere()
    {
        QVERIFY(biome(u"qtcraft:plains"_s).atmosphere.isEmpty());
        QVERIFY(biome(u"qtcraft:desert"_s).atmosphere.isEmpty());
    }

    // Tears from crying obsidian, lava soaking through, souls over soul sand and soul soil.
    void netherBlocksGiveOffParticles()
    {
        using From = QcParticleEmitter::From;
        QCOMPARE(particlesOf(u"qtcraft:crying_obsidian"_s), (QList<QcParticleEmitter>{{QcParticleKind::DrippingTear, 0.2, From::Faces}}));
        QCOMPARE(particlesOf(u"qtcraft:lava"_s), (QList<QcParticleEmitter>{{QcParticleKind::DrippingLava, 0.1, From::Through}}));
        QCOMPARE(particlesOf(u"qtcraft:soul_sand"_s), (QList<QcParticleEmitter>{{QcParticleKind::Soul, 0.002, From::Top}}));
        QCOMPARE(particlesOf(u"qtcraft:soul_soil"_s), (QList<QcParticleEmitter>{{QcParticleKind::Soul, 0.002, From::Top}}));
        QVERIFY(particlesOf(u"qtcraft:netherrack"_s).isEmpty());
    }

    // The nether's building blocks sound of their own (84.1b) and every sound they name exists.
    void netherBlocksHaveTheirOwnSounds_data()
    {
        QTest::addColumn<QString>("block");
        QTest::addColumn<QString>("sound");
        QTest::newRow("nether bricks") << u"qtcraft:nether_bricks"_s << u"nether_bricks"_s;
        QTest::newRow("red nether bricks") << u"qtcraft:red_nether_bricks"_s << u"nether_bricks"_s;
        QTest::newRow("basalt") << u"qtcraft:polished_basalt"_s << u"basalt"_s;
        QTest::newRow("blackstone") << u"qtcraft:gilded_blackstone"_s << u"blackstone"_s;
        QTest::newRow("soul soil") << u"qtcraft:soul_soil"_s << u"soul_soil"_s;
        QTest::newRow("nylium") << u"qtcraft:warped_nylium"_s << u"nylium"_s;
        QTest::newRow("stem") << u"qtcraft:stripped_crimson_stem"_s << u"stem"_s;
        QTest::newRow("planks") << u"qtcraft:warped_planks"_s << u"nether_wood"_s;
        QTest::newRow("wart") << u"qtcraft:nether_wart_block"_s << u"wart_block"_s;
        QTest::newRow("shroomlight") << u"qtcraft:shroomlight"_s << u"shroomlight"_s;
        QTest::newRow("debris") << u"qtcraft:ancient_debris"_s << u"ancient_debris"_s;
        QTest::newRow("netherite") << u"qtcraft:netherite_block"_s << u"netherite"_s;
        QTest::newRow("fungus") << u"qtcraft:crimson_fungus"_s << u"fungus"_s;
        QTest::newRow("roots") << u"qtcraft:nether_sprouts"_s << u"roots"_s;
        QTest::newRow("ore") << u"qtcraft:nether_quartz_ore"_s << u"nether_ore"_s;
    }

    void netherBlocksHaveTheirOwnSounds()
    {
        QFETCH(QString, block);
        QFETCH(QString, sound);
        const QcBlockDefinition &definition = m_base->block(m_base->blockId(block).value_or(qcAirBlockId));
        QCOMPARE(definition.soundMaterial, u"qtcraft:"_s + sound);
        for (const char *action : {"break", "hit", "place", "step"}) {
            const QString name = u"qtcraft:block.%1.%2"_s.arg(sound, QLatin1StringView(action));
            QVERIFY2(m_base->assets().sound(name), qPrintable(name));
        }
    }
};

QTEST_GUILESS_MAIN(TestAtmosphereContent)
#include "test_atmosphere_content.moc"

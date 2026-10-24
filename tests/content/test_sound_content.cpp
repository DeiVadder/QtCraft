#include "common/content/definitions/QcSoundKeys.h"
#include "common/content/loader/QcBaseContent.h"
#include "common/content/parser/QcContentParser.h"
#include "common/content/registry/QcContentSerializer.h"
#include "QcTestUtil.h"
#include "worlds/QcModLoader.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

QJsonObject json(const char *text)
{
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray(text));
    if (!document.isObject()) {
        QcTestUtil::failCase(QStringLiteral("test JSON is not an object: %1").arg(QString::fromUtf8(text)));
        return {};
    }
    return document.object();
}

void writeFile(const QString &path, const char *text)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text);
}

QString key(const QcBlockDefinition &block, QcBlockSoundAction action)
{
    return QcSoundKeys::blockSound(block, action);
}

} // namespace

// Sound data: which sound a block action, a creature or a machine makes, parsing of sound
// definitions, the base game's sounds and sounds from mods.
class TestSoundContent : public QObject
{
    Q_OBJECT

private slots:
    void blockActionsMapToMaterialSounds()
    {
        QcBlockDefinition block;
        block.soundMaterial = u"qtcraft:stone"_s;
        QCOMPARE(key(block, QcBlockSoundAction::Break), u"qtcraft:block.stone.break"_s);
        QCOMPARE(key(block, QcBlockSoundAction::Place), u"qtcraft:block.stone.place"_s);
        QCOMPARE(key(block, QcBlockSoundAction::Step), u"qtcraft:block.stone.step"_s);
        QCOMPARE(key(block, QcBlockSoundAction::Hit), u"qtcraft:block.stone.hit"_s);

        // An explicit key replaces the material's sound for that action only.
        block.soundKeys[static_cast<std::size_t>(QcBlockSoundAction::Step)] = u"mod:squeak"_s;
        QCOMPARE(key(block, QcBlockSoundAction::Step), u"mod:squeak"_s);
        QCOMPARE(key(block, QcBlockSoundAction::Break), u"qtcraft:block.stone.break"_s);

        // A mod's material lives in the mod's namespace.
        block.soundMaterial = u"rubies:crystal"_s;
        QCOMPARE(key(block, QcBlockSoundAction::Hit), u"rubies:block.crystal.hit"_s);

        // Without a material (air, water) a block is silent unless it names a key.
        block.soundMaterial.clear();
        QCOMPARE(key(block, QcBlockSoundAction::Break), QString());
        QCOMPARE(key(block, QcBlockSoundAction::Step), u"mod:squeak"_s);
    }

    void blocksWithoutSoundGetAMaterialFromTheirProperties_data()
    {
        QTest::addColumn<QByteArray>("definition");
        QTest::addColumn<QString>("material");

        QTest::newRow("pickaxe") << QByteArray(R"({"name":"rock","tool":"pickaxe","textures":{"all":"t"}})") << u"qtcraft:stone"_s;
        QTest::newRow("no tool") << QByteArray(R"({"name":"rock","textures":{"all":"t"}})") << u"qtcraft:stone"_s;
        QTest::newRow("axe") << QByteArray(R"({"name":"log","tool":"axe","textures":{"all":"t"}})") << u"qtcraft:wood"_s;
        QTest::newRow("shovel") << QByteArray(R"({"name":"soil","tool":"shovel","textures":{"all":"t"}})") << u"qtcraft:dirt"_s;
        QTest::newRow("plant") << QByteArray(R"({"name":"fern","shape":"cross","textures":{"all":"t"}})") << u"qtcraft:grass"_s;
        QTest::newRow("leaves") << QByteArray(R"({"name":"leaves","render":"cutout","textures":{"all":"t"}})") << u"qtcraft:grass"_s;
        QTest::newRow("glass") << QByteArray(R"({"name":"pane","render":"translucent","textures":{"all":"t"}})") << u"qtcraft:glass"_s;
        QTest::newRow("liquid") << QByteArray(R"({"name":"oil","render":"translucent","liquid":true,"solid":false,"textures":{"all":"t"}})")
                                << QString();
        QTest::newRow("invisible") << QByteArray(R"({"name":"barrier","render":"invisible"})") << QString();
        QTest::newRow("explicit") << QByteArray(R"({"name":"bell","sound":"metal","tool":"axe","textures":{"all":"t"}})") << u"mod:metal"_s;
        QTest::newRow("explicit base") << QByteArray(R"({"name":"rug","sound":"qtcraft:snow","textures":{"all":"t"}})")
                                       << u"qtcraft:snow"_s;
    }

    void blocksWithoutSoundGetAMaterialFromTheirProperties()
    {
        QFETCH(QByteArray, definition);
        QFETCH(QString, material);
        const QcResult<QcBlockDefinition> block = QcContentParser::parseBlock(QJsonDocument::fromJson(definition).object(), u"mod"_s);
        QVERIFY2(block.has_value(), qcErrorText(block));
        QCOMPARE(block->soundMaterial, material);
    }

    void blockMobAndMachineSoundFieldsAreParsed()
    {
        const QcResult<QcBlockDefinition> block = QcContentParser::parseBlock(
            json(R"({"name":"bell","sound":"metal","sounds":{"break":"shatter","step":"qtcraft:block.snow.step"},"textures":{"all":"t"}})"),
            u"mod"_s);
        QVERIFY2(block.has_value(), qcErrorText(block));
        QCOMPARE(key(*block, QcBlockSoundAction::Break), u"mod:shatter"_s);
        QCOMPARE(key(*block, QcBlockSoundAction::Step), u"qtcraft:block.snow.step"_s);
        QCOMPARE(key(*block, QcBlockSoundAction::Place), u"mod:block.metal.place"_s);

        QVERIFY(!QcContentParser::parseBlock(json(R"({"name":"b","sounds":{"step":5},"textures":{"all":"t"}})"), u"mod"_s));
        QVERIFY(!QcContentParser::parseBlock(json(R"({"name":"b","sounds":["x"],"textures":{"all":"t"}})"), u"mod"_s));
        QVERIFY(!QcContentParser::parseBlock(json(R"({"name":"b","sound":"Not Valid","textures":{"all":"t"}})"), u"mod"_s));

        const QcResult<QcBlockDefinition> kiln =
            QcContentParser::parseBlock(json(R"({"name":"kiln","machine":{"category":"firing"},"textures":{"all":"t"}})"), u"mod"_s);
        QVERIFY2(kiln.has_value(), qcErrorText(kiln));
        QCOMPARE(kiln->machine.sound, u"qtcraft:machine.furnace"_s);
        const QcResult<QcBlockDefinition> quiet = QcContentParser::parseBlock(
            json(R"({"name":"kiln","machine":{"category":"firing","sound":"hum"},"textures":{"all":"t"}})"), u"mod"_s);
        QVERIFY2(quiet.has_value(), qcErrorText(quiet));
        QCOMPARE(quiet->machine.sound, u"mod:hum"_s);

        const QcResult<QcMobDefinition> plain =
            QcContentParser::parseMob(json(R"({"name":"blob","model":[{"from":[0,0,0],"to":[1,1,1],"texture":"t"}]})"), u"mod"_s);
        QVERIFY2(plain.has_value(), qcErrorText(plain));
        QCOMPARE(plain->ambientSound, QString());
        QCOMPARE(plain->hurtSound, u"qtcraft:mob.generic.hurt"_s);
        QCOMPARE(plain->deathSound, u"qtcraft:mob.generic.death"_s);
        const QcResult<QcMobDefinition> loud = QcContentParser::parseMob(
            json(R"({"name":"blob","sounds":{"ambient":"gurgle","hurt":"qtcraft:mob.slime.hurt"},"model":[{"from":[0,0,0],"to":[1,1,1],"texture":"t"}]})"),
            u"mod"_s);
        QVERIFY2(loud.has_value(), qcErrorText(loud));
        QCOMPARE(loud->ambientSound, u"mod:gurgle"_s);
        QCOMPARE(loud->hurtSound, u"qtcraft:mob.slime.hurt"_s);
        QCOMPARE(loud->deathSound, u"qtcraft:mob.generic.death"_s);
        QVERIFY(!QcContentParser::parseMob(
            json(R"({"name":"blob","sounds":"loud","model":[{"from":[0,0,0],"to":[1,1,1],"texture":"t"}]})"), u"mod"_s));
    }

    void soundDefinitionsAreParsed()
    {
        const QcResult<QcSoundDefinition> sound = QcContentParser::parseSound(json(R"({
            "name": "chime", "bus": "ambient", "volume": 0.5, "pitchVariation": 0.2, "volumeVariation": 0.3, "drive": 2.5,
            "variants": 4, "loop": true, "range": 30,
            "layers": [
                {"wave": "sine", "frequency": 880, "endFrequency": 440, "start": 0.1, "duration": 1.5, "attack": 0.05,
                 "curve": 1.5, "gain": 0.7, "lowpass": 3000, "highpass": 100, "tremoloRate": 5, "tremoloDepth": 0.25,
                 "bandpass": 1200, "resonance": 8},
                {"wave": "crackle", "density": 700, "duration": 0.5}
            ]})"),
                                                                                    u"mod"_s);
        QVERIFY2(sound.has_value(), qcErrorText(sound));
        QCOMPARE(sound->name, u"mod:chime"_s);
        QCOMPARE(sound->bus, QcSoundBus::Ambient);
        QCOMPARE(sound->volume, 0.5);
        QCOMPARE(sound->pitchVariation, 0.2);
        QCOMPARE(sound->volumeVariation, 0.3);
        QCOMPARE(sound->drive, 2.5);
        QCOMPARE(sound->variants, 4);
        QVERIFY(sound->loop);
        QCOMPARE(sound->range, 30.0);
        QCOMPARE(sound->layers.size(), 2);
        const QcSoundLayer expected{QcSoundWave::Sine, 880.0, 440.0, 0.1, 1.5, 0.05, 1.5, 0.7, 3000.0, 100.0, 5.0, 0.25, 200.0, 1200.0, 8.0};
        QVERIFY(sound->layers[0] == expected);
        QCOMPARE(sound->layers[1].wave, QcSoundWave::Crackle);
        QCOMPARE(sound->layers[1].density, 700.0);
        QCOMPARE(sound->seconds(), 1.6);

        // Defaults: an effect of volume 1 with one variant, a range of 16 m, white noise layers.
        const QcResult<QcSoundDefinition> plain =
            QcContentParser::parseSound(json(R"({"name":"qtcraft:thud","layers":[{"duration":0.1}]})"), u"mod"_s);
        QVERIFY2(plain.has_value(), qcErrorText(plain));
        QCOMPARE(plain->name, u"qtcraft:thud"_s);
        QCOMPARE(plain->bus, QcSoundBus::Effects);
        QCOMPARE(plain->volume, 1.0);
        QCOMPARE(plain->variants, 1);
        QVERIFY(!plain->loop);
        QCOMPARE(plain->range, 16.0);
        QCOMPARE(plain->volumeVariation, 0.0);
        QCOMPARE(plain->drive, 0.0);
        QCOMPARE(plain->layers.constFirst().wave, QcSoundWave::Noise);
        QCOMPARE(plain->layers.constFirst().bandpass, 0.0);
        QCOMPARE(plain->layers.constFirst().resonance, 1.0);
    }

    void badSoundDefinitionsAreRefused_data()
    {
        QTest::addColumn<QByteArray>("definition");
        QTest::addColumn<QString>("message");

        QTest::newRow("no name") << QByteArray(R"({"layers":[{"duration":0.1}]})") << u"'name' is required"_s;
        QTest::newRow("no layers") << QByteArray(R"({"name":"a"})") << u"'layers'"_s;
        QTest::newRow("empty layers") << QByteArray(R"({"name":"a","layers":[]})") << u"'layers'"_s;
        QTest::newRow("nine layers") << QByteArray(R"({"name":"a","layers":[{},{},{},{},{},{},{},{},{}]})") << u"'layers'"_s;
        QTest::newRow("layer not object") << QByteArray(R"({"name":"a","layers":[3]})") << u"objects"_s;
        QTest::newRow("bad wave") << QByteArray(R"({"name":"a","layers":[{"wave":"organ"}]})") << u"'wave'"_s;
        QTest::newRow("bad bus") << QByteArray(R"({"name":"a","bus":"voice","layers":[{}]})") << u"'bus'"_s;
        QTest::newRow("loud") << QByteArray(R"({"name":"a","volume":1.5,"layers":[{}]})") << u"'volume'"_s;
        QTest::newRow("variants") << QByteArray(R"({"name":"a","variants":9,"layers":[{}]})") << u"'variants'"_s;
        QTest::newRow("long") << QByteArray(R"({"name":"a","layers":[{"duration":11}]})") << u"'duration'"_s;
        QTest::newRow("late") << QByteArray(R"({"name":"a","layers":[{"start":10.5}]})") << u"'start'"_s;
        QTest::newRow("infrasound") << QByteArray(R"({"name":"a","layers":[{"wave":"sine","frequency":5}]})") << u"'frequency'"_s;
        QTest::newRow("sweep below hearing") << QByteArray(R"({"name":"a","layers":[{"wave":"sine","endFrequency":5}]})")
                                             << u"out of range"_s;
        QTest::newRow("filter below hearing") << QByteArray(R"({"name":"a","layers":[{"lowpass":3}]})") << u"out of range"_s;
        QTest::newRow("attack beyond end") << QByteArray(R"({"name":"a","layers":[{"duration":0.1,"attack":0.2}]})") << u"'attack'"_s;
        QTest::newRow("gain") << QByteArray(R"({"name":"a","layers":[{"gain":2}]})") << u"'gain'"_s;
        QTest::newRow("text number") << QByteArray(R"({"name":"a","layers":[{"duration":"1"}]})") << u"'duration'"_s;
        QTest::newRow("range") << QByteArray(R"({"name":"a","range":500,"layers":[{}]})") << u"'range'"_s;
        QTest::newRow("band below hearing") << QByteArray(R"({"name":"a","layers":[{"bandpass":5}]})") << u"out of range"_s;
        QTest::newRow("resonance") << QByteArray(R"({"name":"a","layers":[{"bandpass":500,"resonance":50}]})") << u"'resonance'"_s;
        QTest::newRow("volume variation") << QByteArray(R"({"name":"a","volumeVariation":0.8,"layers":[{}]})") << u"'volumeVariation'"_s;
        QTest::newRow("drive") << QByteArray(R"({"name":"a","drive":9,"layers":[{}]})") << u"'drive'"_s;
    }

    void badSoundDefinitionsAreRefused()
    {
        QFETCH(QByteArray, definition);
        QFETCH(QString, message);
        const QJsonDocument document = QJsonDocument::fromJson(definition);
        QVERIFY(document.isObject());
        const QcResult<QcSoundDefinition> sound = QcContentParser::parseSound(document.object(), u"mod"_s);
        QVERIFY(!sound.has_value());
        QVERIFY2(sound.error().contains(message), qPrintable(sound.error()));
    }

    void baseGameHasEverySoundItUses()
    {
        const QcResult<QcBaseContent::Loaded> loaded = QcBaseContent::load();
        QVERIFY2(loaded.has_value(), qcErrorText(loaded));
        QVERIFY2(loaded->warnings.isEmpty(), qPrintable(loaded->warnings.join(u'\n')));

        const QcContentRegistry &content = *loaded->registry;
        const auto blockKey = [&content](const char *name, QcBlockSoundAction action) {
            return key(content.block(*content.blockId(QString::fromLatin1(name))), action);
        };

        QCOMPARE(blockKey("qtcraft:stone", QcBlockSoundAction::Break), u"qtcraft:block.stone.break"_s);
        QCOMPARE(blockKey("qtcraft:grass_block", QcBlockSoundAction::Step), u"qtcraft:block.grass.step"_s);
        QCOMPARE(blockKey("qtcraft:oak_planks", QcBlockSoundAction::Place), u"qtcraft:block.wood.place"_s);
        QCOMPARE(blockKey("qtcraft:gravel", QcBlockSoundAction::Hit), u"qtcraft:block.gravel.hit"_s);
        QCOMPARE(blockKey("qtcraft:sand", QcBlockSoundAction::Step), u"qtcraft:block.sand.step"_s);
        QCOMPARE(blockKey("qtcraft:glass", QcBlockSoundAction::Break), u"qtcraft:block.glass.break"_s);
        QCOMPARE(blockKey("qtcraft:snow_block", QcBlockSoundAction::Step), u"qtcraft:block.snow.step"_s);
        QCOMPARE(blockKey("qtcraft:water", QcBlockSoundAction::Step), QString());
        QCOMPARE(content.block(*content.blockId(u"qtcraft:furnace"_s)).machine.sound, u"qtcraft:machine.furnace"_s);
        QCOMPARE(content.mobs()[*content.mobIndex(u"qtcraft:zombie"_s)].ambientSound, u"qtcraft:mob.zombie.ambient"_s);

        // Every visible block makes all four sounds, and every name in use exists.
        for (const QcBlockDefinition &block : content.blocks()) {
            if (block.renderLayer == QcRenderLayer::Invisible || block.liquid) {
                continue;
            }
            for (int action = 0; action < qcBlockSoundActionCount; ++action) {
                const QString name = key(block, static_cast<QcBlockSoundAction>(action));
                QVERIFY2(content.assets().sound(name), qPrintable(block.name + u": "_s + name));
            }
        }

        // Spreading water and lava make their own sounds.
        for (const char *liquid : {"qtcraft:water", "qtcraft:lava"}) {
            const QString &name = content.block(*content.blockId(QString::fromLatin1(liquid))).flow.sound;
            QVERIFY2(!name.isEmpty() && content.assets().sound(name), liquid);
        }

        for (const QcMobDefinition &mob : content.mobs()) {
            for (const QString &name : {mob.ambientSound, mob.hurtSound, mob.deathSound}) {
                QVERIFY2(content.assets().sound(name), qPrintable(mob.name + u": "_s + name));
            }
        }

        for (const char *name : {QcSoundKeys::playerHurt, QcSoundKeys::playerDeath, QcSoundKeys::playerEat, QcSoundKeys::itemPickup,
                                 QcSoundKeys::waterSplash, QcSoundKeys::waterSwim, QcSoundKeys::uiClick, QcSoundKeys::mobHurt,
                                 QcSoundKeys::mobDeath, QcSoundKeys::machineActive, QcSoundKeys::liquidHiss, QcSoundKeys::bucketFill,
                                 QcSoundKeys::bucketEmpty, QcSoundKeys::bottleCatch, QcSoundKeys::signalClick, QcSoundKeys::pistonOut,
                                 QcSoundKeys::pistonIn, QcSoundKeys::torchFizz}) {
            const QcSoundDefinition *sound = content.assets().sound(QString::fromLatin1(name));
            QVERIFY2(sound, name);
            QCOMPARE(sound->bus, QcSoundBus::Effects);
            QVERIFY(!sound->loop);
        }

        for (const char *name : {QcSoundKeys::ambientWind, QcSoundKeys::ambientCave, QcSoundKeys::ambientWater}) {
            const QcSoundDefinition *sound = content.assets().sound(QString::fromLatin1(name));
            QVERIFY2(sound, name);
            QCOMPARE(sound->bus, QcSoundBus::Ambient);
            QVERIFY(sound->loop);
        }
    }

    // Decision E-879: the block sounds are short noise bodies without a pitch sweep or a hard tone
    // and vary in loudness per play; metal
    // blocks clank; storage blocks sound as they open and close; an explosion is a long, saturated boom.
    void baseSoundDesign()
    {
        const QcResult<QcBaseContent::Loaded> loaded = QcBaseContent::load();
        QVERIFY2(loaded.has_value(), qcErrorText(loaded));
        const QcContentRegistry &content = *loaded->registry;

        for (const char *material : {"stone", "wood", "dirt", "gravel", "sand", "grass", "snow", "glass", "wool", "metal"}) {
            for (int action = 0; action < qcBlockSoundActionCount; ++action) {
                const QString name = u"qtcraft:block.%1.%2"_s.arg(QString::fromLatin1(material), QcSoundKeys::actionName(static_cast<QcBlockSoundAction>(action)));
                const QcSoundDefinition *sound = content.assets().sound(name);
                QVERIFY2(sound, qPrintable(name));
                QVERIFY2(sound->volumeVariation > 0.0, qPrintable(name));
                for (const QcSoundLayer &layer : sound->layers) {
                    QVERIFY2(layer.wave == QcSoundWave::Noise || layer.wave == QcSoundWave::Crackle, qPrintable(name));
                }
            }
        }

        const auto block = [&content](const char *name) -> const QcBlockDefinition & {
            return content.block(*content.blockId(QString::fromLatin1(name)));
        };
        QCOMPARE(key(block("qtcraft:iron_block"), QcBlockSoundAction::Place), u"qtcraft:block.metal.place"_s);
        QCOMPARE(key(block("qtcraft:anvil"), QcBlockSoundAction::Hit), u"qtcraft:block.metal.hit"_s);

        const QList<std::pair<const char *, QString>> containers = {
            {"qtcraft:chest", u"chest"_s}, {"qtcraft:trapped_chest", u"chest"_s}, {"qtcraft:barrel", u"barrel"_s}, {"qtcraft:backpack", u"backpack"_s}};
        for (const auto &[name, sound] : containers) {
            QCOMPARE(block(name).containerOpenSound, u"qtcraft:block.%1.open"_s.arg(sound));
            QCOMPARE(block(name).containerCloseSound, u"qtcraft:block.%1.close"_s.arg(sound));
            QVERIFY(content.assets().sound(block(name).containerOpenSound));
            QVERIFY(content.assets().sound(block(name).containerCloseSound));
        }
        QCOMPARE(block("qtcraft:signal_hopper").containerOpenSound, QString());

        const QcSoundDefinition *explosion = content.assets().sound(QString::fromLatin1(QcSoundKeys::explosion));
        QVERIFY(explosion);
        QVERIFY(explosion->drive > 0.0);
        QCOMPARE(explosion->range, QcSoundDefinition::maxRange);
        QVERIFY(explosion->seconds() >= 3.0);
    }

    // The content a server sends its clients carries the sound fields of decision E-879.
    void soundFieldsSurviveTheContentTransfer()
    {
        QcContentRegistry content;
        QcSoundDefinition sound;
        sound.name = u"t:knock"_s;
        sound.volumeVariation = 0.3;
        sound.drive = 2.5;
        QcSoundLayer layer;
        layer.wave = QcSoundWave::Noise;
        layer.duration = 0.2;
        layer.bandpass = 900.0;
        layer.resonance = 7.0;
        sound.layers = {layer};
        content.assets().registerSound(sound);

        QByteArray bytes;
        {
            QDataStream out(&bytes, QIODevice::WriteOnly);
            QcContentSerializer::write(content, out);
        }
        QDataStream in(bytes);
        const QcResult<QcContentRegistry> copy = QcContentSerializer::read(in);
        QVERIFY2(copy.has_value(), qcErrorText(copy));
        const QcSoundDefinition *received = copy->assets().sound(sound.name);
        QVERIFY(received);
        QCOMPARE(*received, sound);
        QCOMPARE(received->layers[0].bandpass, 900.0);
        QCOMPARE(received->drive, 2.5);
    }

    void modsAddReplaceAndReferenceSounds()
    {
        QTemporaryDir dir;
        const QString mod = dir.filePath(u"bells"_s);
        writeFile(mod + u"/pack.json"_s, R"({"id":"bells","version":"1.0"})");
        writeFile(mod + u"/sounds/chime.json"_s, R"({"name":"chime","layers":[{"wave":"sine","frequency":1200,"duration":0.4}]})");

        // Replaces a base sound completely.
        writeFile(mod + u"/sounds/stone_break.json"_s,
                  R"({"name":"qtcraft:block.stone.break","volume":0.3,"layers":[{"wave":"square","frequency":300,"duration":0.2}]})");
        writeFile(mod + u"/sounds/broken.json"_s, R"({"name":"broken","layers":[{"wave":"kazoo"}]})");
        writeFile(mod + u"/blocks/bell.json"_s,
                  R"({"name":"bell","sound":"qtcraft:glass","sounds":{"hit":"chime"},"textures":{"all":"qtcraft:glass"}})");
        writeFile(mod + u"/blocks/mute.json"_s, R"({"name":"mute","sounds":{"step":"nothing"},"textures":{"all":"qtcraft:glass"}})");
        writeFile(mod + u"/mobs/ghost.json"_s,
                  R"({"name":"ghost","sounds":{"ambient":"boo"},"model":[{"from":[0,0,0],"to":[1,1,1],"texture":"qtcraft:glass"}]})");

        const QcModLoader::Loaded loaded = QcModLoader::load(dir.path());
        QVERIFY2(loaded.has_value(), qcErrorText(loaded));
        const QcContentRegistry &content = *loaded->content;
        QVERIFY(content.assets().sound(u"bells:chime"_s));
        const QcSoundDefinition *stone = content.assets().sound(u"qtcraft:block.stone.break"_s);
        QVERIFY(stone);
        QCOMPARE(stone->volume, 0.3);
        QCOMPARE(stone->layers.constFirst().wave, QcSoundWave::Square);
        QVERIFY(!content.assets().sound(u"bells:broken"_s));

        const QcBlockDefinition &bell = content.block(*content.blockId(u"bells:bell"_s));
        QCOMPARE(key(bell, QcBlockSoundAction::Hit), u"bells:chime"_s);
        QCOMPARE(key(bell, QcBlockSoundAction::Break), u"qtcraft:block.glass.break"_s);

        // Broken definitions and names nothing defines are reported; such blocks and mobs stay silent.
        const QString all = loaded->warnings.join(u'\n');
        QVERIFY2(all.contains(u"broken.json"_s) && all.contains(u"'wave'"_s), qPrintable(all));
        QVERIFY2(all.contains(u"block bells:mute: unknown sound 'bells:nothing'"_s), qPrintable(all));
        QVERIFY2(all.contains(u"mob bells:ghost: unknown sound 'bells:boo'"_s), qPrintable(all));
        QVERIFY2(!all.contains(u"bells:bell"_s), qPrintable(all));

        // The sounds reach clients with the content: the loader checked them like a client.
        QVERIFY(std::ranges::any_of(loaded->mods, [](const QcModStatus &status) {
            return status.info.id == u"bells"_s && status.state == QcModStatus::State::Loaded;
        }));
    }

    void exampleModBringsItsOwnSounds()
    {
        const QcModLoader::Loaded loaded = QcModLoader::load(QStringLiteral(QTCRAFT_MODS_SOURCE_DIR));
        QVERIFY2(loaded.has_value(), qcErrorText(loaded));
        QVERIFY2(loaded->warnings.isEmpty(), qPrintable(loaded->warnings.join(u'\n')));

        const QcContentRegistry &content = *loaded->content;
        const QcBlockDefinition &ruby = content.block(*content.blockId(u"rubies:ruby_block"_s));
        QCOMPARE(key(ruby, QcBlockSoundAction::Break), u"rubies:block.crystal.break"_s);
        QVERIFY(content.assets().sound(u"rubies:block.crystal.break"_s));

        // The ore keeps the base game's stone sounds.
        QCOMPARE(key(content.block(*content.blockId(u"rubies:ruby_ore"_s)), QcBlockSoundAction::Break), u"qtcraft:block.stone.break"_s);
        const QcMobDefinition &beetle = content.mobs()[*content.mobIndex(u"rubies:crystal_beetle"_s)];
        QCOMPARE(beetle.deathSound, u"rubies:mob.crystal_beetle.death"_s);
        QVERIFY(content.assets().sound(beetle.ambientSound));
    }
};

QTEST_GUILESS_MAIN(TestSoundContent)
#include "test_sound_content.moc"

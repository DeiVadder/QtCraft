// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/sound/QcAudioMixer.h"
#include "clientcore/sound/QcSoundBank.h"
#include "clientcore/sound/QcSoundCategorizer.h"
#include "common/content/registry/QcSoundKeys.h"
#include "fixtures/QcTestUtil.h"

#include <QTest>

#include <cmath>
#include <memory>

using namespace Qt::StringLiterals;

Q_DECLARE_METATYPE(QcSoundCategory)

namespace {

// `samples` samples of the constant `value` at 1000 Hz.
std::shared_ptr<const QcSampleBuffer> constant(float value, int samples)
{
    auto buffer = std::make_shared<QcSampleBuffer>();
    buffer->sampleRate = 1000;
    buffer->samples.assign(static_cast<std::size_t>(samples), value);
    return buffer;
}

} // namespace

// The sound categories behind the volume sliders (decision E-1150): which category each sound
// has, and that the mixer applies each category's volume to exactly its voices.
class TestSoundCategories : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { m_content = QcTestUtil::loadContent(); }

    void baseSoundsHaveTheirCategory_data()
    {
        QTest::addColumn<QString>("sound");
        QTest::addColumn<QcSoundCategory>("category");
        QTest::newRow("block break") << u"qtcraft:block.stone.break"_s << QcSoundCategory::Blocks;
        QTest::newRow("machine") << QString::fromLatin1(QcSoundKeys::machineActive) << QcSoundCategory::Blocks;
        QTest::newRow("note block") << u"qtcraft:note.harp"_s << QcSoundCategory::Blocks;
        QTest::newRow("explosion") << QString::fromLatin1(QcSoundKeys::explosion) << QcSoundCategory::Blocks;
        QTest::newRow("zombie") << u"qtcraft:mob.zombie.hurt"_s << QcSoundCategory::Hostile;
        QTest::newRow("skeleton") << u"qtcraft:mob.skeleton.ambient"_s << QcSoundCategory::Hostile;
        QTest::newRow("cow") << u"qtcraft:mob.cow.ambient"_s << QcSoundCategory::Friendly;
        QTest::newRow("cow milked") << u"qtcraft:mob.cow.milk"_s << QcSoundCategory::Friendly;
        QTest::newRow("fish (no creature of that name)") << u"qtcraft:mob.fish.hurt"_s << QcSoundCategory::Friendly;
        QTest::newRow("common hurt") << QString::fromLatin1(QcSoundKeys::mobHurt) << QcSoundCategory::Friendly;
        QTest::newRow("player hurt") << QString::fromLatin1(QcSoundKeys::playerHurt) << QcSoundCategory::Players;
        QTest::newRow("item pickup") << QString::fromLatin1(QcSoundKeys::itemPickup) << QcSoundCategory::Players;
        QTest::newRow("bow") << QString::fromLatin1(QcSoundKeys::bowShoot) << QcSoundCategory::Players;
        QTest::newRow("splash") << QString::fromLatin1(QcSoundKeys::waterSplash) << QcSoundCategory::Players;
        QTest::newRow("rain") << QString::fromLatin1(QcSoundKeys::ambientRain) << QcSoundCategory::Weather;
        QTest::newRow("thunder") << QString::fromLatin1(QcSoundKeys::weatherThunder) << QcSoundCategory::Weather;
        QTest::newRow("wind") << QString::fromLatin1(QcSoundKeys::ambientWind) << QcSoundCategory::Ambient;
        QTest::newRow("biome loop") << u"qtcraft:ambient.crimson_forest.loop"_s << QcSoundCategory::Ambient;
        QTest::newRow("flowing water (ambient bus)") << u"qtcraft:liquid.water.flow"_s << QcSoundCategory::Ambient;
        QTest::newRow("ui click") << QString::fromLatin1(QcSoundKeys::uiClick) << QcSoundCategory::Interface;
    }

    void baseSoundsHaveTheirCategory()
    {
        QFETCH(QString, sound);
        QFETCH(QcSoundCategory, category);
        const QcSoundDefinition *definition = m_content->assets().sound(sound);
        QVERIFY2(definition, qPrintable(sound));
        QCOMPARE(QcSoundCategorizer(*m_content).category(*definition), category);
    }

    // A mod's creatures and sounds follow the same rules in their own namespace; a sound on the
    // music bus is music whatever its name.
    void modSoundsFollowTheirCreatures()
    {
        QcContentRegistry content;
        QcMobDefinition beetle;
        beetle.name = u"mod:beetle"_s;
        beetle.hostile = true;
        beetle.hurtSound = u"mod:mob.bug.hurt"_s;
        beetle.ambientSound = u"mod:beetle.hiss"_s;
        beetle.deathSound = u"mod:block.wood.break"_s;
        content.registerMob(beetle);
        QcMobDefinition moth;
        moth.name = u"mod:moth"_s;
        moth.hurtSound = u"mod:mob.wings.hurt"_s;
        moth.ambientSound = u"mod:flutter"_s;
        content.registerMob(moth);
        QcMobDefinition wasp = beetle;
        wasp.name = u"mod:wasp"_s;
        wasp.hurtSound = u"mod:mob.wings.hurt"_s;
        wasp.ambientSound.clear();
        content.registerMob(wasp);
        const QcSoundCategorizer categorizer(content);

        const auto categoryOf = [&categorizer](const QString &name, QcSoundBus bus = QcSoundBus::Effects) {
            QcSoundDefinition sound;
            sound.name = name;
            sound.bus = bus;
            return categorizer.category(sound);
        };
        QCOMPARE(categoryOf(u"mod:mob.beetle.chirp"_s), QcSoundCategory::Hostile);
        QCOMPARE(categoryOf(u"mod:mob.moth.flutter"_s), QcSoundCategory::Friendly);
        // Named only by the hostile beetle; named by a hostile and a friendly creature.
        QCOMPARE(categoryOf(u"mod:mob.bug.hurt"_s), QcSoundCategory::Hostile);
        QCOMPARE(categoryOf(u"mod:mob.wings.hurt"_s), QcSoundCategory::Friendly);
        // Creature sounds named outside the mob scheme follow their creature; a block sound a
        // creature borrows stays with the blocks.
        QCOMPARE(categoryOf(u"mod:beetle.hiss"_s), QcSoundCategory::Hostile);
        QCOMPARE(categoryOf(u"mod:flutter"_s), QcSoundCategory::Friendly);
        QCOMPARE(categoryOf(u"mod:block.wood.break"_s), QcSoundCategory::Blocks);
        // The creature of another namespace does not count.
        QCOMPARE(categoryOf(u"other:mob.beetle.chirp"_s), QcSoundCategory::Friendly);
        QCOMPARE(categoryOf(u"mod:block.crystal.break"_s), QcSoundCategory::Blocks);
        QCOMPARE(categoryOf(u"mod:song"_s, QcSoundBus::Music), QcSoundCategory::Music);
        QCOMPARE(categoryOf(u"mod:hum"_s, QcSoundBus::Ambient), QcSoundCategory::Ambient);
        QCOMPARE(categoryOf(u"mod:whatever"_s), QcSoundCategory::Players);
    }

    // The sound bank keeps each sound's category, so the engine plays it there.
    void theBankKeepsTheCategory()
    {
        QcContentRegistry content;
        for (const char *name : {QcSoundKeys::uiClick, QcSoundKeys::playerHurt}) {
            content.assets().registerSound(*m_content->assets().sound(QString::fromLatin1(name)));
        }
        const QcSoundBank bank = QcSoundBank::build(content, 8000);
        QCOMPARE(bank.find(QString::fromLatin1(QcSoundKeys::uiClick))->category, QcSoundCategory::Interface);
        QCOMPARE(bank.find(QString::fromLatin1(QcSoundKeys::playerHurt))->category, QcSoundCategory::Players);
    }

    // One voice per category, every category at its own volume: each voice is heard at exactly
    // its category's volume, under the master volume (mixed by hand, no device and no clock).
    void eachCategoryVolumeAppliesToItsVoices()
    {
        for (const QcSoundCategory heard : QcSoundCategories::all()) {
            QcAudioMixer mixer(1000);
            mixer.setMasterVolume(0.5f);
            for (const QcSoundCategory category : QcSoundCategories::all()) {
                mixer.setCategoryVolume(category, category == heard ? 0.8f : 0.0f);
                QVERIFY(mixer.play(constant(0.25f, 100), {1.0f, -1.0f, 1.0, category, false}) != 0);
            }

            const std::span<const float> out = mixer.mix(4);
            for (std::size_t frame = 0; frame < 4; ++frame) {
                QVERIFY2(std::abs(out[frame * 2] - 0.25f * 0.8f * 0.5f) < 1e-6f, qPrintable(QcSoundCategories::name(heard)));
                QCOMPARE(out[frame * 2 + 1], 0.0f);
            }
        }
    }

    void namesAreStableForTheSettingsFile()
    {
        QStringList names;
        for (const QcSoundCategory category : QcSoundCategories::all()) {
            names.append(QcSoundCategories::name(category));
            QCOMPARE(QcSoundCategories::fromName(names.constLast()), category);
            QCOMPARE(QcSoundCategories::fromIndex(static_cast<int>(category)), category);
        }
        QCOMPARE(names, (QStringList{u"music"_s, u"weather"_s, u"blocks"_s, u"hostile"_s, u"friendly"_s, u"players"_s, u"ambient"_s, u"interface"_s}));
        QCOMPARE(QcSoundCategories::fromName(u"effects"_s), std::nullopt);
        QCOMPARE(QcSoundCategories::fromIndex(qcSoundCategoryCount), std::nullopt);
    }

private:
    std::shared_ptr<QcContentRegistry> m_content;
};

QTEST_MAIN(TestSoundCategories)
#include "test_sound_categories.moc"

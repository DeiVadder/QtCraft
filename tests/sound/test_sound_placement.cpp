// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcClientEntities.h"
#include "clientcore/session/QcClientWorld.h"
#include "clientcore/sound/QcAudioMixer.h"
#include "clientcore/sound/QcPositionalVoices.h"
#include "clientcore/sound/QcSoundSpatial.h"
#include "clientcore/sound/QcSoundSynthesizer.h"
#include "common/content/registry/QcSoundKeys.h"
#include "fixtures/QcTestUtil.h"

#include <QTest>

#include <cmath>
#include <memory>
#include <tuple>

using namespace Qt::StringLiterals;

namespace {

QcSoundDefinition definition(double range)
{
    QcSoundDefinition sound;
    sound.name = u"t.sound"_s;
    sound.range = range;
    sound.volume = 1.0;
    return sound;
}

QcSoundCue cueAt(const QcVec3 &position, const QString &key = u"t.sound"_s)
{
    return {key, position, 1.0f, 1.0f, QcSoundCue::Origin::Server, 0};
}

// A second of silence-free signal at 1000 Hz sample rate: long enough to still play while the
// test moves the listener.
std::shared_ptr<const QcSampleBuffer> longBuffer()
{
    auto buffer = std::make_shared<QcSampleBuffer>();
    buffer->sampleRate = 1000;
    buffer->samples.assign(1000, 0.5f);
    return buffer;
}

// The engine's way to start a positional cue (QcAudioEngine::play), without an output device.
QcAudioMixer::VoiceId start(QcAudioMixer &mixer, QcPositionalVoices &voices, const QcSoundSpatial::Source &source,
                            const QcSoundSpatial::Listener &listener, const QString &key)
{
    const QcSoundSpatial::Placement placement = QcSoundSpatial::hear(source, listener);
    const QcAudioMixer::VoiceId id = voices.start(mixer, longBuffer(), key, source, {placement.gain, placement.pan, 1.0, QcSoundBus::Effects, false});
    return id;
}

// The share of the output samples above the limiter's knee while `blasts` explosions of `buffer`
// start a quarter second apart next to the listener at the server's TNT volume (1.1). `stacked`
// starts them as the engine does (QcPositionalVoices), otherwise straight in the mixer.
double shareAboveKnee(const std::shared_ptr<const QcSampleBuffer> &buffer, const QcSoundDefinition &sound, int blasts, bool stacked)
{
    QcAudioMixer mixer(buffer->sampleRate);
    QcPositionalVoices voices;
    const QcSoundSpatial::Listener listener{{0.0, 64.0, 0.0}, 0.0};
    const QcSoundCue cue{QString::fromLatin1(QcSoundKeys::explosion), listener.ears, 1.1f, 1.0f, QcSoundCue::Origin::Server, 0};
    const QcSoundSpatial::Source source = QcSoundSpatial::source(sound, cue);
    const QcSoundSpatial::Placement placement = QcSoundSpatial::hear(source, listener);
    const QcAudioMixer::PlayParameters parameters{placement.gain, placement.pan, 1.0, QcSoundBus::Effects, false};

    qsizetype above = 0;
    qsizetype total = 0;
    const auto mixFor = [&](int frames) {
        for (const float sample : mixer.mix(frames)) {
            above += std::abs(sample) > QcAudioMixer::limiterKnee ? 1 : 0;
            ++total;
        }
    };
    for (int i = 0; i < blasts; ++i) {
        if (stacked) {
            voices.start(mixer, buffer, cue.key, source, parameters);
        } else {
            mixer.play(buffer, parameters);
        }
        mixFor(buffer->sampleRate / 4);
    }
    mixFor(static_cast<int>(buffer->samples.size()));
    return static_cast<double>(above) / static_cast<double>(total);
}

} // namespace

// Where positional sounds are heard from: loudness over distance, voices that follow the listener,
// many sources of one sound, and the position other creatures' steps sound at.
class TestSoundPlacement : public QObject
{
    Q_OBJECT

private slots:
    void gainFallsWithDistance()
    {
        const QcSoundDefinition sound = definition(16.0);
        // The listener looks north (-Z), the sources lie ahead of him.
        const QcSoundSpatial::Listener listener{{0.0, 64.0, 0.0}, 0.0};
        const auto gainAt = [&](double metres) {
            return QcSoundSpatial::hear(QcSoundSpatial::source(sound, cueAt({0.0, 64.0, -metres})), listener).gain;
        };

        const float near = gainAt(2.0);
        const float middle = gainAt(8.0);
        const float far = gainAt(15.0);
        qInfo() << "gain at 2 m" << near << "8 m" << middle << "15 m" << far;
        QVERIFY(near > middle);
        QVERIFY(middle > far);
        QVERIFY(far > 0.0f);
        // Clearly audible differences: (1 - d / 16)^2.
        QVERIFY(qFuzzyCompare(near, 0.765625f));
        QVERIFY(qFuzzyCompare(middle, 0.25f));
        QCOMPARE(gainAt(16.5), 0.0f);

        // A cue louder than 1 carries farther, as far as the server sends it.
        QcSoundCue loud = cueAt({0.0, 64.0, -20.0});
        loud.volume = 2.0f;
        const QcSoundSpatial::Source source = QcSoundSpatial::source(sound, loud);
        QCOMPARE(source.range, 32.0);
        QVERIFY(QcSoundSpatial::hear(source, listener).gain > 0.0f);
    }

    void runningVoicesFollowTheListener()
    {
        QcAudioMixer mixer(1000);
        QcPositionalVoices voices;
        const QcSoundSpatial::Source source = QcSoundSpatial::source(definition(16.0), cueAt({0.0, 64.0, -2.0}));
        const QcAudioMixer::VoiceId id = start(mixer, voices, source, {{0.0, 64.0, 0.0}, 0.0}, u"t.sound"_s);
        const float startGain = mixer.voices().constFirst().gain;
        QCOMPARE(mixer.voices().constFirst().pan, 0.0f);

        // Walked away: quieter while it plays.
        voices.update(mixer, {{0.0, 64.0, 12.0}, 0.0});
        QVERIFY(mixer.voices().constFirst().gain < startGain * 0.2f);
        // Turned east: the sound is on the left now.
        voices.update(mixer, {{0.0, 64.0, 0.0}, 90.0});
        QVERIFY(mixer.voices().constFirst().pan < -0.5f);

        // A voice that ended is forgotten.
        mixer.stop(id);
        voices.update(mixer, {{0.0, 64.0, 0.0}, 0.0});
        QVERIFY(voices.voices().isEmpty());
    }

    void manyWaterSourcesStayBounded()
    {
        QcAudioMixer mixer(1000);
        QcPositionalVoices voices;
        QcSoundDefinition flow = definition(16.0);
        flow.volume = 0.35;
        const QcSoundSpatial::Listener listener{{0.0, 64.0, 0.0}, 0.0};
        // Fifty flowing water blocks all around the listener, each sounding once.
        for (int i = 0; i < 50; ++i) {
            const QcVec3 position{(i % 7) - 3.0, 63.0, std::floor(i / 7.0) - 3.0};
            start(mixer, voices, QcSoundSpatial::source(flow, cueAt(position, u"liquid.water.flow"_s)), listener, u"liquid.water.flow"_s);
        }

        // The voices that gave way fade out within a few milliseconds and are gone then.
        QVERIFY(mixer.voiceCount() > QcPositionalVoices::maxPerKey);
        QCOMPARE(voices.voices().size(), QcPositionalVoices::maxPerKey);
        std::ignore = mixer.mix(QcAudioMixer::fadeOutMilliseconds * 2);
        QCOMPARE(mixer.voiceCount(), QcPositionalVoices::maxPerKey);
        float total = 0.0f;
        for (const QcAudioMixer::Voice &voice : mixer.voices()) {
            total += voice.gain;
        }
        QVERIFY(total <= QcPositionalVoices::maxPerKey * 0.35f);

        // Other sounds have their own share.
        start(mixer, voices, QcSoundSpatial::source(definition(16.0), cueAt({1.0, 64.0, 0.0})), listener, u"t.sound"_s);
        QCOMPARE(mixer.voiceCount(), QcPositionalVoices::maxPerKey + 1);
    }

    // A single TNT blast stays below the limiter's knee; a chain of four overlapping ones, each quieter
    // by the explosions still sounding, spends far less time in the limiter than at full level
    // (decision E-879).
    void explosionChainsStayOutOfTheLimiter()
    {
        const std::shared_ptr<const QcContentRegistry> content = QcTestUtil::loadContent();
        const QcSoundDefinition *sound = content->assets().sound(QString::fromLatin1(QcSoundKeys::explosion));
        QVERIFY(sound);
        // A low rate keeps the rendering quick; the envelope and the levels are the same.
        const auto buffer = std::make_shared<const QcSampleBuffer>(QcSoundSynthesizer::render(*sound, 0, 12000));

        QCOMPARE(shareAboveKnee(buffer, *sound, 1, true), 0.0);
        const double plain = shareAboveKnee(buffer, *sound, 4, false);
        const double stacked = shareAboveKnee(buffer, *sound, 4, true);
        qInfo() << "share above the knee, four blasts: plain" << plain << "stacked" << stacked;
        QVERIFY2(stacked < 0.5 * plain, qPrintable(u"%1 vs %2"_s.arg(stacked).arg(plain)));
        QVERIFY(stacked < 0.01);

        QCOMPARE(QcPositionalVoices::stackedGain(0), 1.0f);
        QCOMPARE(QcPositionalVoices::stackedGain(3), 0.5f);
    }

    void creatureStepsSoundWhereTheCreatureIs()
    {
        auto content = std::make_shared<const QcContentRegistry>();
        const QcClientWorld world(content);
        QcClientEntities entities;
        entities.setBuffered(false);
        QcMsg::EntitySpawn spawn;
        spawn.id = 5;
        spawn.kind = QcEntityKind::Player;
        spawn.position = {100.5, 65.0, -40.5};
        spawn.flags = QcMsg::OnGround;
        QVERIFY(entities.spawn(spawn, content.get()));

        QList<QcSoundCue> cues;
        QcVec3 position = spawn.position;
        for (int tick = 0; tick < 5; ++tick) {
            position.x += 0.5;
            entities.moves(QcMsg::EntityMoves{{QcMsg::EntityMove{5, position, 0.0f, 20.0, false, 0.0f, QcMsg::OnGround, qcNoItem}}, static_cast<qint64>(tick) * 50});
            entities.tick();
            entities.stepSounds(world, [&cues](const QcSoundCue &cue) { cues.append(cue); });
        }

        QCOMPARE(cues.size(), 1);
        // Where the walker stood when it completed the stride, far from the origin and the listener.
        QVERIFY(std::abs(cues[0].position.x - 102.0) <= 0.5);
        QCOMPARE(cues[0].position.y, 65.0);
        QCOMPARE(cues[0].position.z, -40.5);
    }
};

QTEST_GUILESS_MAIN(TestSoundPlacement)
#include "test_sound_placement.moc"

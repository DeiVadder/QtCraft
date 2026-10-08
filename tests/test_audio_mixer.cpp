#include "QcAudioMixer.h"
#include "QcSoundSpatial.h"

#include <QTest>

#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

namespace {

// A buffer holding `count` samples of the same value: the mix of one voice is then the value times
// the gains, which the tests compute by hand.
std::shared_ptr<const QcSampleBuffer> constant(float value, std::size_t count, int rate = 1000)
{
    auto buffer = std::make_shared<QcSampleBuffer>();
    buffer->samples.assign(count, value);
    buffer->sampleRate = rate;
    return buffer;
}

std::vector<float> mixFrames(QcAudioMixer &mixer, int frames)
{
    std::vector<float> out(std::size_t(frames) * 2, -7.0f);
    mixer.mix(out.data(), frames);
    return out;
}

const auto centre = float(1.0 / std::numbers::sqrt2);

} // namespace

// The software mixer (buses, master, mute, voice limit, loops, limiter) and the placement of sounds
// around the listener (distance falloff, left/right, behind).
class TestAudioMixer : public QObject
{
    Q_OBJECT

private slots:
    void panningKeepsThePower()
    {
        const auto [leftC, rightC] = QcAudioMixer::panGains(0.0f);
        QVERIFY(std::abs(leftC - centre) < 1e-6f);
        QVERIFY(std::abs(rightC - centre) < 1e-6f);
        const auto [leftL, rightL] = QcAudioMixer::panGains(-1.0f);
        QVERIFY(std::abs(leftL - 1.0f) < 1e-6f);
        QVERIFY(std::abs(rightL) < 1e-6f);
        const auto [leftR, rightR] = QcAudioMixer::panGains(1.0f);
        QVERIFY(std::abs(leftR) < 1e-6f);
        QVERIFY(std::abs(rightR - 1.0f) < 1e-6f);
        // Half right: angle 3/8 pi.
        const auto [leftH, rightH] = QcAudioMixer::panGains(0.5f);
        QVERIFY(std::abs(leftH - float(std::cos(3.0 * std::numbers::pi / 8.0))) < 1e-6f);
        QVERIFY(std::abs(rightH - float(std::sin(3.0 * std::numbers::pi / 8.0))) < 1e-6f);
    }

    void limiterNeverReachesFullScale()
    {
        QCOMPARE(QcAudioMixer::limit(0.5f), 0.5f);
        QCOMPARE(QcAudioMixer::limit(-0.75f), -0.75f);
        // Rising with the input until float precision saturates, never reaching 1.
        float previous = 0.75f;
        for (const float input : {0.8f, 1.0f, 2.0f, 10.0f, 1000.0f}) {
            const float output = QcAudioMixer::limit(input);
            const bool rising = input > 2.0f ? output >= previous : output > previous;
            QVERIFY2(rising && output < 1.0f, qPrintable(QString::number(input)));
            QCOMPARE(QcAudioMixer::limit(-input), -output);
            previous = output;
        }
        QCOMPARE(QcAudioMixer::limit(std::numeric_limits<float>::quiet_NaN()), 0.0f);
        QCOMPARE(QcAudioMixer::limit(std::numeric_limits<float>::infinity()), 0.0f);
    }

    void voiceGainBusMasterAndMuteMultiply()
    {
        QcAudioMixer mixer(1000);
        QVERIFY(mixer.play(constant(0.5f, 100), {0.8f, 0.0f, 1.0, QcSoundBus::Effects, false}) != 0);
        mixer.setBusVolume(QcSoundBus::Effects, 0.5f);
        mixer.setMasterVolume(0.5f);
        const std::vector<float> out = mixFrames(mixer, 10);
        const float expected = 0.5f * 0.8f * 0.5f * 0.5f * centre;
        for (const float sample : out) {
            QVERIFY(std::abs(sample - expected) < 1e-6f);
        }
        // Other buses do not change this voice; muting silences everything.
        mixer.setBusVolume(QcSoundBus::Music, 0.0f);
        QVERIFY(std::abs(mixFrames(mixer, 1)[0] - expected) < 1e-6f);
        mixer.setMuted(true);
        for (const float sample : mixFrames(mixer, 10)) {
            QCOMPARE(sample, 0.0f);
        }
        mixer.setMuted(false);
        mixer.setBusVolume(QcSoundBus::Effects, 0.0f);
        QCOMPARE(mixFrames(mixer, 1)[1], 0.0f);
        // Volumes are clamped to 0..1.
        mixer.setMasterVolume(3.0f);
        QCOMPARE(mixer.masterVolume(), 1.0f);
        mixer.setBusVolume(QcSoundBus::Ambient, std::numeric_limits<float>::quiet_NaN());
        QCOMPARE(mixer.busVolume(QcSoundBus::Ambient), 0.0f);
    }

    void voicesPanAndAdd()
    {
        QcAudioMixer mixer(1000);
        mixer.play(constant(0.2f, 50), {1.0f, -1.0f, 1.0, QcSoundBus::Effects, false});
        mixer.play(constant(0.3f, 50), {1.0f, 1.0f, 1.0, QcSoundBus::Ambient, false});
        const std::vector<float> out = mixFrames(mixer, 4);
        QVERIFY(std::abs(out[0] - 0.2f) < 1e-6f);
        QVERIFY(std::abs(out[1] - 0.3f) < 1e-6f);
    }

    void voicesEndOrLoop()
    {
        QcAudioMixer mixer(1000);
        const QcAudioMixer::VoiceId once = mixer.play(constant(0.5f, 10), {1.0f, -1.0f, 1.0, QcSoundBus::Effects, false});
        const QcAudioMixer::VoiceId loop = mixer.play(constant(0.25f, 10), {1.0f, 1.0f, 1.0, QcSoundBus::Ambient, true});
        const std::vector<float> out = mixFrames(mixer, 25);
        // The one-shot plays its ten samples, the loop keeps going.
        QVERIFY(std::abs(out[18] - 0.5f) < 1e-6f);
        QVERIFY(std::abs(out[20]) < 1e-6f);
        QVERIFY(std::abs(out[49] - 0.25f) < 1e-6f);
        QVERIFY(!mixer.isPlaying(once));
        QVERIFY(mixer.isPlaying(loop));
        QCOMPARE(mixer.voiceCount(), 1);
        mixer.stop(loop);
        QCOMPARE(mixer.voiceCount(), 0);
        QVERIFY(mixFrames(mixer, 3) == std::vector<float>(6, 0.0f));
    }

    void pitchAndSampleRateChangeTheSpeed()
    {
        // A 100-sample buffer at 1000 Hz lasts 100 output frames at 1000 Hz, 50 at pitch 2 and 200
        // at an output rate of 2000 Hz.
        const auto framesUntilDone = [](int outputRate, double pitch) {
            QcAudioMixer mixer(outputRate);
            const QcAudioMixer::VoiceId id = mixer.play(constant(0.1f, 100), {1.0f, 0.0f, pitch, QcSoundBus::Effects, false});
            int frames = 0;
            while (mixer.isPlaying(id) && frames < 1000) {
                mixFrames(mixer, 1);
                ++frames;
            }
            return frames;
        };
        QCOMPARE(framesUntilDone(1000, 1.0), 100);
        QCOMPARE(framesUntilDone(1000, 2.0), 50);
        QCOMPARE(framesUntilDone(2000, 1.0), 200);
    }

    void interpolatesBetweenSamples()
    {
        auto ramp = std::make_shared<QcSampleBuffer>();
        ramp->samples = {0.0f, 0.4f, 0.8f};
        ramp->sampleRate = 1000;
        QcAudioMixer mixer(2000);
        mixer.play(ramp, {1.0f, -1.0f, 1.0, QcSoundBus::Effects, false});
        const std::vector<float> out = mixFrames(mixer, 4);
        // Positions 0, 0.5, 1, 1.5 source samples.
        QVERIFY(std::abs(out[0] - 0.0f) < 1e-6f);
        QVERIFY(std::abs(out[2] - 0.2f) < 1e-6f);
        QVERIFY(std::abs(out[4] - 0.4f) < 1e-6f);
        QVERIFY(std::abs(out[6] - 0.6f) < 1e-6f);
    }

    void gainChangesRampOverOneBlock()
    {
        QcAudioMixer mixer(1000);
        const QcAudioMixer::VoiceId id = mixer.play(constant(1.0f, 1000), {0.0f, -1.0f, 1.0, QcSoundBus::Effects, true});
        mixer.setGain(id, 0.5f);
        const std::vector<float> out = mixFrames(mixer, 10);
        // From 0 towards 0.5 in ten equal steps, no jump.
        for (int frame = 0; frame < 10; ++frame) {
            QVERIFY2(std::abs(out[std::size_t(2 * frame)] - 0.05f * float(frame + 1)) < 1e-5f, qPrintable(QString::number(frame)));
        }
        QVERIFY(std::abs(mixFrames(mixer, 1)[0] - 0.5f) < 1e-6f);
    }

    void voiceLimitReplacesTheQuietestOneShot()
    {
        QcAudioMixer mixer(1000);
        QList<QcAudioMixer::VoiceId> ids;
        for (int i = 0; i < QcAudioMixer::maxVoices; ++i) {
            // Voice 5 is the quietest; voices 0..3 loop and are never replaced.
            const float gain = i == 5 ? 0.01f : 0.5f;
            ids.append(mixer.play(constant(0.1f, 1000), {gain, 0.0f, 1.0, QcSoundBus::Effects, i < 4}));
        }
        QCOMPARE(mixer.voiceCount(), QcAudioMixer::maxVoices);
        const QcAudioMixer::VoiceId extra = mixer.play(constant(0.1f, 1000), {0.9f, 0.0f, 1.0, QcSoundBus::Effects, false});
        QVERIFY(extra != 0);
        QCOMPARE(mixer.voiceCount(), QcAudioMixer::maxVoices);
        QVERIFY(!mixer.isPlaying(ids[5]));
        for (int i = 0; i < 4; ++i) {
            QVERIFY(mixer.isPlaying(ids[i]));
        }
        // Quieter than every one-shot left (0.5): refused, nothing else stops.
        QCOMPARE(mixer.play(constant(0.1f, 1000), {0.2f, 0.0f, 1.0, QcSoundBus::Effects, false}), QcAudioMixer::VoiceId(0));
        QVERIFY(mixer.isPlaying(extra));

        // Music is never replaced, even when it is the quietest voice.
        QcAudioMixer busy(1000);
        const QcAudioMixer::VoiceId music = busy.play(constant(0.1f, 1000), {0.01f, 0.0f, 1.0, QcSoundBus::Music, false});
        for (int i = 1; i < QcAudioMixer::maxVoices; ++i) {
            busy.play(constant(0.1f, 1000), {0.5f, 0.0f, 1.0, QcSoundBus::Effects, false});
        }
        QVERIFY(busy.play(constant(0.1f, 1000), {0.9f, 0.0f, 1.0, QcSoundBus::Effects, false}) != 0);
        QVERIFY(busy.isPlaying(music));

        // When every voice loops, a new sound is refused instead.
        QcAudioMixer loops(1000);
        for (int i = 0; i < QcAudioMixer::maxVoices; ++i) {
            loops.play(constant(0.1f, 10), {0.1f, 0.0f, 1.0, QcSoundBus::Ambient, true});
        }
        QCOMPARE(loops.play(constant(0.1f, 10), {1.0f, 0.0f, 1.0, QcSoundBus::Effects, false}), QcAudioMixer::VoiceId(0));
        QCOMPARE(loops.voiceCount(), QcAudioMixer::maxVoices);
    }

    void manyLoudVoicesStayBelowFullScale()
    {
        QcAudioMixer mixer(1000);
        for (int i = 0; i < QcAudioMixer::maxVoices; ++i) {
            mixer.play(constant(0.9f, 100), {1.0f, 0.0f, 1.0, QcSoundBus::Effects, false});
        }
        for (const float sample : mixFrames(mixer, 50)) {
            QVERIFY(sample > 0.75f && sample < 1.0f);
        }
    }

    void emptyOrBrokenBuffersAreRefused()
    {
        QcAudioMixer mixer(1000);
        QCOMPARE(mixer.play(nullptr, {}), QcAudioMixer::VoiceId(0));
        QCOMPARE(mixer.play(std::make_shared<QcSampleBuffer>(), {}), QcAudioMixer::VoiceId(0));
        const QcAudioMixer::VoiceId odd =
            mixer.play(constant(0.5f, 10), {std::numeric_limits<float>::quiet_NaN(), 9.0f, std::numeric_limits<double>::infinity(),
                                            QcSoundBus::Effects, false});
        QVERIFY(odd != 0);
        const QcAudioMixer::Voice &voice = mixer.voices().constFirst();
        QCOMPARE(voice.gain, 0.0f);
        QCOMPARE(voice.pan, 1.0f);
        QCOMPARE(voice.pitch, 1.0);
    }

    void distanceAttenuation()
    {
        QCOMPARE(QcSoundSpatial::attenuation(0.0, 16.0), 1.0f);
        // (1 - 8/16)^2 and (1 - 12/16)^2.
        QCOMPARE(QcSoundSpatial::attenuation(8.0, 16.0), 0.25f);
        QCOMPARE(QcSoundSpatial::attenuation(12.0, 16.0), 0.0625f);
        QCOMPARE(QcSoundSpatial::attenuation(16.0, 16.0), 0.0f);
        QCOMPARE(QcSoundSpatial::attenuation(100.0, 16.0), 0.0f);
        QCOMPARE(QcSoundSpatial::attenuation(std::numeric_limits<double>::quiet_NaN(), 16.0), 0.0f);
        QCOMPARE(QcSoundSpatial::attenuation(1.0, 0.0), 0.0f);
    }

    void panFollowsTheViewDirection_data()
    {
        QTest::addColumn<double>("yaw");
        QTest::addColumn<double>("x");
        QTest::addColumn<double>("z");
        QTest::addColumn<float>("pan");
        // Yaw 0 looks north (-Z): east (+X) is on the right.
        QTest::newRow("north, east") << 0.0 << 5.0 << 0.0 << QcSoundSpatial::panWidth;
        QTest::newRow("north, west") << 0.0 << -5.0 << 0.0 << -QcSoundSpatial::panWidth;
        QTest::newRow("north, ahead") << 0.0 << 0.0 << -5.0 << 0.0f;
        QTest::newRow("north, behind") << 0.0 << 0.0 << 5.0 << 0.0f;
        // Yaw 90 looks east: south (+Z) is on the right.
        QTest::newRow("east, south") << 90.0 << 0.0 << 5.0 << QcSoundSpatial::panWidth;
        QTest::newRow("east, north") << 90.0 << 0.0 << -5.0 << -QcSoundSpatial::panWidth;
        // Yaw 180 looks south: east is on the left now.
        QTest::newRow("south, east") << 180.0 << 5.0 << 0.0 << -QcSoundSpatial::panWidth;
        // 45 degrees to the right front: sin(45°) of the width.
        QTest::newRow("north, front right") << 0.0 << 3.0 << -3.0 << float(QcSoundSpatial::panWidth * std::sqrt(0.5));
        QTest::newRow("straight above") << 0.0 << 0.0 << 0.0 << 0.0f;
    }

    void panFollowsTheViewDirection()
    {
        QFETCH(double, yaw);
        QFETCH(double, x);
        QFETCH(double, z);
        QFETCH(float, pan);
        const QcVec3 listener{10.0, 70.0, -20.0};
        const float result = QcSoundSpatial::pan(listener, yaw, listener + QcVec3{x, 4.0, z});
        QVERIFY2(std::abs(result - pan) < 1e-5f, qPrintable(QString::number(result)));
    }

    void soundsBehindAreQuieter()
    {
        const QcVec3 listener{0.0, 64.0, 0.0};
        const QcSoundSpatial::Placement ahead = QcSoundSpatial::place(listener, 0.0, {0.0, 64.0, -4.0}, 16.0);
        const QcSoundSpatial::Placement behind = QcSoundSpatial::place(listener, 0.0, {0.0, 64.0, 4.0}, 16.0);
        const QcSoundSpatial::Placement side = QcSoundSpatial::place(listener, 0.0, {4.0, 64.0, 0.0}, 16.0);
        // 4 m of 16 m: (3/4)^2.
        QCOMPARE(ahead.gain, 0.5625f);
        QCOMPARE(side.gain, 0.5625f);
        QCOMPARE(behind.gain, 0.5625f * (1.0f - QcSoundSpatial::rearDamping));
        QCOMPARE(ahead.distance, 4.0);
        QCOMPARE(behind.pan, 0.0f);
        const QcSoundSpatial::Placement far = QcSoundSpatial::place(listener, 0.0, {0.0, 80.0, 0.0}, 16.0);
        QCOMPARE(far.gain, 0.0f);
    }
};

QTEST_GUILESS_MAIN(TestAudioMixer)
#include "test_audio_mixer.moc"

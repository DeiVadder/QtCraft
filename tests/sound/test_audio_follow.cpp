// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/QcAudioOutput.h"

#include <QSignalSpy>
#include <QTest>

#include <chrono>
#include <memory>

using namespace std::chrono_literals;

namespace {

// The system's audio as the test scripts it: devices with the rates they take, which of them open,
// and sink errors on demand. Synchronous, without a thread or a real device.
class FakeAudio : public QcAudioPlatform
{
    Q_OBJECT

public:
    struct FakeDevice
    {
        QByteArray id;
        // The first is the device's own rate.
        QList<int> rates;
        bool opens = true;
    };

    QList<FakeDevice> devices;
    QByteArray defaultId;
    QByteArray openId;
    int opens = 0;
    quint64 generation = 0;

    FakeDevice *find(const QByteArray &id)
    {
        for (FakeDevice &device : devices) {
            if (device.id == id) {
                return &device;
            }
        }
        return nullptr;
    }

    [[nodiscard]] QList<Device> outputs() const override
    {
        QList<Device> list;
        for (const FakeDevice &device : devices) {
            list.append({device.id, QString::fromLatin1(device.id)});
        }
        return list;
    }

    [[nodiscard]] QByteArray defaultOutput() const override { return defaultId; }

    [[nodiscard]] std::optional<Format> format(const QByteArray &id, int keepRate) const override
    {
        for (const FakeDevice &device : devices) {
            if (device.id == id) {
                return Format{device.rates.contains(keepRate) ? keepRate : device.rates.constFirst(), QcMixerDevice::Format::Float};
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] Opened open(const QByteArray &id, const Format &, const QcMixerDevice::Render &) override
    {
        close();
        ++opens;
        ++generation;
        const FakeDevice *device = find(id);
        if (!device || !device->opens) {
            return {QtAudio::OpenError, generation};
        }
        openId = id;
        return {QtAudio::NoError, generation};
    }

    void close() override { openId.clear(); }
    void watchOutputs(bool) override {}

    // The devices changed (call after changing `devices` or `defaultId`).
    void change() { emit outputsChanged(); }
    // The open sink stops with an error, as WASAPI's after a format change of the same device.
    void breakSink()
    {
        openId.clear();
        emit failed(generation, static_cast<int>(QtAudio::IOError));
    }
};

std::shared_ptr<const QcSampleBuffer> longSound(int rate)
{
    auto buffer = std::make_shared<QcSampleBuffer>();
    buffer->samples.assign(static_cast<std::size_t>(rate), 0.1f);
    buffer->sampleRate = rate;
    return buffer;
}

struct Rig
{
    FakeAudio *audio = nullptr;
    std::unique_ptr<QcAudioOutput> output;

    Rig(QList<FakeAudio::FakeDevice> devices, QByteArray defaultId)
    {
        auto platform = std::make_unique<FakeAudio>();
        audio = platform.get();
        audio->devices = std::move(devices);
        audio->defaultId = std::move(defaultId);
        output = std::make_unique<QcAudioOutput>(std::move(platform));
    }

    QcAudioMixer::VoiceId play() const
    {
        QcAudioMixer::VoiceId id = 0;
        output->withMixer([&id](QcAudioMixer &mixer) { id = mixer.play(longSound(mixer.outputRate()), {}); });
        return id;
    }

    bool playing(QcAudioMixer::VoiceId id) const
    {
        bool result = false;
        output->withMixer([&](QcAudioMixer &mixer) { result = mixer.isPlaying(id); });
        return result;
    }
};

} // namespace

// QcAudioOutput on a fake platform: which device and rate it plays at after device changes, a
// device that does not open, and sink errors while playing.
class TestAudioFollow : public QObject
{
    Q_OBJECT

private slots:
    void aDeviceAtTheSameRateKeepsTheVoices()
    {
        Rig rig({{"speakers", {48000}}, {"headset", {44100, 48000}}}, "speakers");
        QSignalSpy resets(rig.output.get(), &QcAudioOutput::mixerReset);
        QVERIFY(rig.output->start());
        QCOMPARE(rig.output->sampleRate(), 48000);
        QCOMPARE(resets.count(), 1);
        const QcAudioMixer::VoiceId voice = rig.play();

        rig.audio->defaultId = "headset";
        rig.audio->change();
        QCOMPARE(rig.output->deviceId(), QByteArray("headset"));
        QCOMPARE(rig.output->sampleRate(), 48000);
        QVERIFY(rig.playing(voice));
        QCOMPARE(resets.count(), 1);
    }

    void aDeviceAtAnotherRateResetsTheMixer()
    {
        Rig rig({{"speakers", {48000}}, {"headset", {44100}}}, "speakers");
        QSignalSpy resets(rig.output.get(), &QcAudioOutput::mixerReset);
        QVERIFY(rig.output->start());
        const QcAudioMixer::VoiceId voice = rig.play();

        rig.audio->defaultId = "headset";
        rig.audio->change();
        QCOMPARE(rig.output->sampleRate(), 44100);
        QVERIFY(!rig.playing(voice));
        QCOMPARE(resets.count(), 2);
    }

    void aDeviceThatDoesNotOpenKeepsTheMixer()
    {
        Rig rig({{"speakers", {48000}}, {"headset", {44100}, false}}, "speakers");
        // No retry gets in the way of the device changes below.
        rig.output->setRetryDelay(1h);
        QSignalSpy resets(rig.output.get(), &QcAudioOutput::mixerReset);
        QVERIFY(rig.output->start());
        const QcAudioMixer::VoiceId voice = rig.play();

        // The headset at 44.1 kHz does not open: silent, the mixer stays at 48 kHz with its voice.
        rig.audio->defaultId = "headset";
        rig.audio->change();
        QVERIFY(!rig.output->isActive());
        QVERIFY(!rig.output->error().isEmpty());
        QCOMPARE(rig.output->sampleRate(), 48000);
        QVERIFY(rig.playing(voice));
        QCOMPARE(resets.count(), 1);

        // Back on the speakers at the old rate: the same mixer, the voice plays on, no reset.
        rig.audio->defaultId = "speakers";
        rig.audio->change();
        QCOMPARE(rig.output->deviceId(), QByteArray("speakers"));
        QCOMPARE(rig.output->sampleRate(), 48000);
        QVERIFY(rig.playing(voice));
        QCOMPARE(resets.count(), 1);
        QVERIFY(rig.output->error().isEmpty());
    }

    void aSinkErrorOpensTheDeviceAgain()
    {
        Rig rig({{"speakers", {48000}}}, "speakers");
        rig.output->setRetryDelay(5ms);
        QVERIFY(rig.output->start());
        const QcAudioMixer::VoiceId voice = rig.play();

        // The device stays the default, so no device change follows the error.
        rig.audio->breakSink();
        QVERIFY(!rig.output->isActive());
        QVERIFY(!rig.output->error().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(rig.output->isActive(), 2000);
        QCOMPARE(rig.audio->opens, 2);
        QCOMPARE(rig.audio->openId, QByteArray("speakers"));
        QVERIFY(rig.output->error().isEmpty());
        QVERIFY(rig.playing(voice));
    }

    void retriesAreBounded()
    {
        Rig rig({{"speakers", {48000}}}, "speakers");
        rig.output->setRetryDelay(1ms);
        QVERIFY(rig.output->start());

        rig.audio->find("speakers")->opens = false;
        rig.audio->breakSink();
        QTRY_COMPARE_WITH_TIMEOUT(rig.audio->opens, 1 + QcAudioOutput::maxRetries, 2000);
        // Gave up: no further attempt until the devices change.
        QTest::qWait(50);
        QCOMPARE(rig.audio->opens, 1 + QcAudioOutput::maxRetries);
        QVERIFY(!rig.output->isActive());

        rig.audio->find("speakers")->opens = true;
        rig.audio->change();
        QVERIFY(rig.output->isActive());
    }

    void aLateErrorOfAnOldSinkIsIgnored()
    {
        Rig rig({{"speakers", {48000}}, {"headset", {48000}}}, "speakers");
        QVERIFY(rig.output->start());
        const quint64 old = rig.audio->generation;
        rig.audio->defaultId = "headset";
        rig.audio->change();
        QCOMPARE(rig.output->deviceId(), QByteArray("headset"));

        emit rig.audio->failed(old, static_cast<int>(QtAudio::IOError));
        QVERIFY(rig.output->isActive());
        QCOMPARE(rig.output->deviceId(), QByteArray("headset"));
    }
};

QTEST_GUILESS_MAIN(TestAudioFollow)
#include "test_audio_follow.moc"

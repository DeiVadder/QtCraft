// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/music/QcVorbisStream.h"
#include "clientcore/sound/QcAudioMixer.h"
#include "clientcore/sound/QcStreamCursor.h"

#include <QFile>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace Qt::StringLiterals;

namespace {

const QString dataDirectory = QStringLiteral(QTCRAFT_SOURCE_DIR "/tests/data/music");

// A stream whose frame n is (n, -n) scaled by `scale`, `frames` long, read in pieces of at most
// `piece` frames (as a decoder hands out its packets).
class CountingStream final : public QcAudioStream
{
public:
    CountingStream(int frames, int rate, float scale = 1.0f, int piece = 300)
        : m_frames(frames)
        , m_rate(rate)
        , m_scale(scale)
        , m_piece(piece)
    {
    }

    [[nodiscard]] int sampleRate() const override { return m_rate; }
    [[nodiscard]] double seconds() const override { return static_cast<double>(m_frames) / m_rate; }
    [[nodiscard]] int read(std::span<float> stereo) override
    {
        const int count = std::min({static_cast<int>(stereo.size() / 2), m_piece, m_frames - m_next});
        for (int i = 0; i < count; ++i) {
            stereo[2 * static_cast<std::size_t>(i)] = static_cast<float>(m_next + i) * m_scale;
            stereo[2 * static_cast<std::size_t>(i) + 1] = -static_cast<float>(m_next + i) * m_scale;
        }
        m_next += count;
        return count;
    }

private:
    int m_frames = 0;
    int m_rate = 0;
    float m_scale = 1.0f;
    int m_piece = 0;
    int m_next = 0;
};

// Decodes all of `stream` in pieces of 1000 frames: the frames, interleaved.
std::vector<float> decodeAll(QcAudioStream &stream)
{
    std::vector<float> all;
    std::vector<float> piece(2000);
    for (int read = stream.read(piece); read > 0; read = stream.read(piece)) {
        all.insert(all.end(), piece.begin(), piece.begin() + 2 * static_cast<std::ptrdiff_t>(read));
    }
    return all;
}

// The largest magnitude of one channel (0 left, 1 right) of interleaved frames.
float peak(const std::vector<float> &frames, std::size_t channel)
{
    float largest = 0.0f;
    for (std::size_t i = channel; i < frames.size(); i += 2) {
        largest = std::max(largest, std::abs(frames[i]));
    }
    return largest;
}

} // namespace

// Music decoded while it plays (decision E-1135): the Ogg Vorbis decoder on two tiny files (half a
// second of 440 Hz, stereo with a silent right side, and mono), the cursor that slides along a
// stream, and stream voices in the mixer with their long fade.
class TestMusicStream : public QObject
{
    Q_OBJECT

private slots:
    void decodesStereo()
    {
        QcResult<std::unique_ptr<QcVorbisStream>> stream = QcVorbisStream::open(dataDirectory + u"/stereo_tone.ogg"_s);
        QVERIFY2(stream, qPrintable(stream ? QString() : stream.error()));
        QCOMPARE((*stream)->sampleRate(), 44100);
        QCOMPARE((*stream)->channels(), 2);
        QVERIFY(std::abs((*stream)->seconds() - 0.5) < 0.01);

        const std::vector<float> frames = decodeAll(**stream);
        QVERIFY2(std::abs(static_cast<int>(frames.size() / 2) - 22050) <= 64, qPrintable(QString::number(frames.size() / 2)));
        // FFmpeg's sine has an amplitude of 1/8: the left side carries it, the right is silent.
        QVERIFY2(peak(frames, 0) > 0.1f && peak(frames, 0) < 0.16f, qPrintable(QString::number(peak(frames, 0))));
        QVERIFY2(peak(frames, 1) < 0.01f, qPrintable(QString::number(peak(frames, 1))));
        QCOMPARE((*stream)->read(std::span<float>()), 0);
    }

    void decodesMonoOnBothSides()
    {
        QFile file(dataDirectory + u"/mono_tone.ogg"_s);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QcResult<std::unique_ptr<QcVorbisStream>> stream = QcVorbisStream::fromBytes(file.readAll());
        QVERIFY(stream);
        QCOMPARE((*stream)->channels(), 1);

        const std::vector<float> frames = decodeAll(**stream);
        QVERIFY(std::abs(static_cast<int>(frames.size() / 2) - 22050) <= 64);
        QVERIFY(peak(frames, 0) > 0.1f);
        for (std::size_t i = 0; i < frames.size(); i += 2) {
            QCOMPARE(frames[i + 1], frames[i]);
        }
    }

    void refusesWhatIsNoOggVorbis()
    {
        QVERIFY(!QcVorbisStream::open(dataDirectory + u"/missing.ogg"_s));
        QVERIFY(!QcVorbisStream::open(u":/no/such/resource.ogg"_s));
        QVERIFY(!QcVorbisStream::fromBytes(QByteArray()));
        QVERIFY(!QcVorbisStream::fromBytes(QByteArray(4096, 'x')));
        QFile file(dataDirectory + u"/stereo_tone.ogg"_s);
        QVERIFY(file.open(QIODevice::ReadOnly));
        // The headers cut off.
        QVERIFY(!QcVorbisStream::fromBytes(file.read(200)));
    }

    // A file cut off after 60 % of its bytes (headers whole, a download or copy that broke off):
    // decoded up to the cut, then the stream ends; no hang, no error.
    void decodesACutOffFileToItsEnd()
    {
        QFile file(dataDirectory + u"/long_tone.ogg"_s);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray whole = file.readAll();
        QcResult<std::unique_ptr<QcVorbisStream>> stream = QcVorbisStream::fromBytes(whole.left(whole.size() * 6 / 10));
        QVERIFY2(stream, qPrintable(stream ? QString() : stream.error()));
        QCOMPARE((*stream)->sampleRate(), 48000);

        const std::vector<float> frames = decodeAll(**stream);
        const auto decoded = static_cast<int>(frames.size() / 2);
        // 8 s at 48 kHz in the whole file; well over a third and less than all of it here.
        QVERIFY2(decoded > 3 * 48000 && decoded < 8 * 48000, qPrintable(QString::number(decoded)));
        QCOMPARE((*stream)->read(std::span<float>()), 0);
    }

    // The cursor interpolates between frames, also across its window's edges, and ends after the
    // last frame.
    void cursorSlidesAlongTheStream()
    {
        constexpr int frames = 3 * QcStreamCursor::windowFrames + 17;
        QcStreamCursor cursor(std::make_shared<CountingStream>(frames, 1000));
        for (double position : {0.0, 10.5, 299.75, 1023.5, 1024.0, 2047.25, 3000.5}) {
            const std::optional<std::array<float, 2>> frame = cursor.frameAt(position);
            QVERIFY2(frame, qPrintable(QString::number(position)));
            QCOMPARE((*frame)[0], static_cast<float>(position));
            QCOMPARE((*frame)[1], -static_cast<float>(position));
        }
        QVERIFY(!cursor.ended());
        // The last frame has no right neighbour: it is held.
        QCOMPARE((*cursor.frameAt(frames - 0.5))[0], static_cast<float>(frames - 1));
        QVERIFY(!cursor.frameAt(frames));
        QVERIFY(cursor.ended());
    }

    // A stream voice keeps its stereo sides at their level in the centre, steps through the stream
    // at the stream's rate and leaves the mixer at its end.
    void mixerPlaysAStream()
    {
        QcAudioMixer mixer(1000);
        const QcAudioMixer::VoiceId id = mixer.playStream(std::make_shared<QcStreamCursor>(std::make_shared<CountingStream>(100, 500, 0.01f)),
                                                          {0.5f, 0.0f, 1.0, QcSoundBus::Music, true});
        QVERIFY(id != 0);
        QCOMPARE(mixer.voices().constFirst().loop, false);
        const std::span<const float> mixed = mixer.mix(10);
        // Half the stream's rate: frame 3 of the output is frame 1.5 of the stream.
        QVERIFY(std::abs(mixed[6] - 0.5f * 0.015f) < 1e-6f);
        QVERIFY(std::abs(mixed[7] + 0.5f * 0.015f) < 1e-6f);

        [[maybe_unused]] const std::span<const float> rest = mixer.mix(300);
        QVERIFY(!mixer.isPlaying(id));
        QVERIFY(mixer.playStream(nullptr, {}) == 0);
    }

    // A 44.1 kHz stream on a 48 kHz output: the mixer steps through it at 44100 / 48000 frames per
    // output frame, interpolating linearly.
    void mixerResamplesAStream()
    {
        QcAudioMixer mixer(48000);
        const QcAudioMixer::VoiceId id = mixer.playStream(std::make_shared<QcStreamCursor>(std::make_shared<CountingStream>(44100, 44100, 0.00001f)),
                                                          {1.0f, 0.0f, 1.0, QcSoundBus::Music, false});
        QVERIFY(id != 0);
        const std::span<const float> mixed = mixer.mix(4800);
        for (const std::size_t frame : {std::size_t{1}, std::size_t{480}, std::size_t{4799}}) {
            const float expected = static_cast<float>(static_cast<double>(frame) * 44100.0 / 48000.0) * 0.00001f;
            QVERIFY2(std::abs(mixed[2 * frame] - expected) < 1e-6f, qPrintable(u"%1: %2, not %3"_s.arg(frame).arg(mixed[2 * frame]).arg(expected)));
            QVERIFY(std::abs(mixed[2 * frame + 1] + expected) < 1e-6f);
        }
    }

    // A long fade (the music's on a change of place) lowers the voice over its whole time, then
    // removes it.
    void longFadeOut()
    {
        QcAudioMixer mixer(1000);
        const QcAudioMixer::VoiceId id = mixer.playStream(std::make_shared<QcStreamCursor>(std::make_shared<CountingStream>(10000, 1000, 0.0001f)),
                                                          {1.0f, 0.0f, 1.0, QcSoundBus::Music, false});
        mixer.fadeOut(id, 400);
        const std::vector<float> first = [&] {
            const std::span<const float> block = mixer.mix(200);
            return std::vector<float>(block.begin(), block.end());
        }();
        // Half way through the fade the gain is a half: frame 199 carries 0.0199 at about 0.5.
        QVERIFY2(std::abs(first[std::size_t{2} * 199] - 0.0199f * 0.5f) < 0.0002f, qPrintable(QString::number(first[std::size_t{2} * 199])));
        QVERIFY(mixer.isPlaying(id));
        [[maybe_unused]] const std::span<const float> second = mixer.mix(200);
        QVERIFY(!mixer.isPlaying(id));
    }
};

QTEST_GUILESS_MAIN(TestMusicStream)
#include "test_music_stream.moc"

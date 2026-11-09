// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/sound/QcAudioStream.h"
#include "common/core/QcResult.h"

#include <QByteArray>
#include <QString>

#include <memory>

struct stb_vorbis;

/*! \brief An Ogg Vorbis file decoded while it plays (decision E-1135), through stb_vorbis.

    The file's compressed bytes stay in memory (a few megabytes; a file in the Qt resources is used
    where it lies, without a copy), the decoded audio never as a whole: read() decodes the next
    frames. Opening reads the headers and sets the decoder up (a few milliseconds; the engine opens
    on a worker thread); decoding allocates nothing on the heap. */
class QcVorbisStream final : public QcAudioStream
{
public:
    /*! Largest file accepted, in bytes. */
    static constexpr qint64 maxFileBytes = 64LL * 1024 * 1024;
    /*! Most channels accepted. */
    static constexpr int maxChannels = 2;

    /*! Returns the stream of the file at \a path (on disk or in the Qt resources, ":/…"); an error
        when it cannot be read, is too large or is no Ogg Vorbis with one or two channels. */
    [[nodiscard]] static QcResult<std::unique_ptr<QcVorbisStream>> open(const QString &path);
    /*! Returns the stream of the Ogg Vorbis file in \a bytes (kept as long as the stream lives); an
        error when it is none. */
    [[nodiscard]] static QcResult<std::unique_ptr<QcVorbisStream>> fromBytes(QByteArray bytes);

    ~QcVorbisStream() override;
    QcVorbisStream(const QcVorbisStream &) = delete;
    QcVorbisStream &operator=(const QcVorbisStream &) = delete;
    QcVorbisStream(QcVorbisStream &&) = delete;
    QcVorbisStream &operator=(QcVorbisStream &&) = delete;

    [[nodiscard]] int sampleRate() const override { return m_sampleRate; }
    [[nodiscard]] double seconds() const override { return m_seconds; }
    /*! Returns the channels of the file (1 or 2). */
    [[nodiscard]] int channels() const { return m_channels; }
    [[nodiscard]] int read(std::span<float> stereo) override;

private:
    QcVorbisStream(QByteArray bytes, stb_vorbis *decoder);

    QByteArray m_bytes;
    stb_vorbis *m_decoder = nullptr;
    int m_sampleRate = 0;
    int m_channels = 0;
    double m_seconds = 0.0;
};

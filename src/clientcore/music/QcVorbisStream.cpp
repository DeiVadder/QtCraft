// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/music/QcVorbisStream.h"

#include <QFile>
#include <QResource>

// Only the declarations; the code is built into qc_stb_vorbis (decision E-1135).
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c> // NOLINT(bugprone-suspicious-include): the library ships as one .c file

using namespace Qt::StringLiterals;

namespace {

// The file's bytes: a resource without compression where it lies, anything else read (a
// folder of the resources has no data).
QcResult<QByteArray> fileBytes(const QString &path)
{
    if (path.startsWith(u':')) {
        const QResource resource(path);
        if (!resource.isValid() || resource.data() == nullptr) {
            return qcError(u"%1 is no resource"_s.arg(path));
        }
        if (resource.uncompressedSize() > QcVorbisStream::maxFileBytes) {
            return qcError(u"%1 is larger than %2 bytes"_s.arg(path).arg(QcVorbisStream::maxFileBytes));
        }
        if (resource.compressionAlgorithm() == QResource::NoCompression) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): QResource holds bytes as uchar, QByteArray as char
            return QByteArray::fromRawData(reinterpret_cast<const char *>(resource.data()), static_cast<qsizetype>(resource.size()));
        }
        return resource.uncompressedData();
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return qcError(u"Cannot read %1: %2"_s.arg(path, file.errorString()));
    }
    if (file.size() > QcVorbisStream::maxFileBytes) {
        return qcError(u"%1 is larger than %2 bytes"_s.arg(path).arg(QcVorbisStream::maxFileBytes));
    }
    return file.readAll();
}

} // namespace

QcResult<std::unique_ptr<QcVorbisStream>> QcVorbisStream::open(const QString &path)
{
    QcResult<QByteArray> bytes = fileBytes(path);
    if (!bytes) {
        return qcError(bytes.error());
    }

    QcResult<std::unique_ptr<QcVorbisStream>> stream = fromBytes(std::move(*bytes));
    if (!stream) {
        return qcError(u"%1: %2"_s.arg(path, stream.error()));
    }
    return stream;
}

QcResult<std::unique_ptr<QcVorbisStream>> QcVorbisStream::fromBytes(QByteArray bytes)
{
    if (bytes.isEmpty() || bytes.size() > maxFileBytes) {
        return qcError(u"no Ogg Vorbis data of a usable size"_s);
    }

    int error = 0;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): stb_vorbis takes bytes as unsigned char, QByteArray holds char
    stb_vorbis *decoder = stb_vorbis_open_memory(reinterpret_cast<const unsigned char *>(bytes.constData()), static_cast<int>(bytes.size()), &error, nullptr);
    if (!decoder) {
        return qcError(u"not an Ogg Vorbis file (stb_vorbis error %1)"_s.arg(error));
    }

    const stb_vorbis_info info = stb_vorbis_get_info(decoder);
    if (info.channels < 1 || info.channels > maxChannels || info.sample_rate == 0) {
        stb_vorbis_close(decoder);
        return qcError(u"%1 channels at %2 Hz cannot be played"_s.arg(info.channels).arg(info.sample_rate));
    }
    // The constructor is private: make_unique cannot call it.
    return std::unique_ptr<QcVorbisStream>(new QcVorbisStream(std::move(bytes), decoder));
}

QcVorbisStream::QcVorbisStream(QByteArray bytes, stb_vorbis *decoder)
    : m_bytes(std::move(bytes))
    , m_decoder(decoder)
    , m_sampleRate(static_cast<int>(stb_vorbis_get_info(decoder).sample_rate))
    , m_channels(stb_vorbis_get_info(decoder).channels)
    , m_seconds(static_cast<double>(stb_vorbis_stream_length_in_seconds(decoder)))
{
}

QcVorbisStream::~QcVorbisStream()
{
    stb_vorbis_close(m_decoder);
}

int QcVorbisStream::read(std::span<float> stereo)
{
    const auto frames = static_cast<int>(stereo.size() / 2);
    if (frames <= 0) {
        return 0;
    }
    if (m_channels == 2) {
        return stb_vorbis_get_samples_float_interleaved(m_decoder, 2, stereo.data(), frames * 2);
    }

    // Mono: decoded into the front half, then spread to both sides from the back.
    const int read = stb_vorbis_get_samples_float_interleaved(m_decoder, 1, stereo.data(), frames);
    for (int i = read - 1; i >= 0; --i) {
        const float sample = stereo[static_cast<std::size_t>(i)];
        stereo[2 * static_cast<std::size_t>(i)] = sample;
        stereo[2 * static_cast<std::size_t>(i) + 1] = sample;
    }
    return read;
}

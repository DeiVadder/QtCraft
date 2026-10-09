#include "QcSoundExporter.h"

#include "QcContentRegistry.h"
#include "QcMusicComposer.h"
#include "QcSoundSynthesizer.h"

#include <QDataStream>
#include <QDir>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

QcStatus writeWav(const QString &path, const QcSampleBuffer &buffer)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return qcError(u"Cannot write %1"_s.arg(path));
    }

    const auto dataBytes = quint32(buffer.samples.size() * sizeof(qint16));
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + dataBytes);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(1) << quint32(buffer.sampleRate) << quint32(buffer.sampleRate * 2) << quint16(2)
           << quint16(16);
    stream.writeRawData("data", 4);
    stream << dataBytes;
    for (const float sample : buffer.samples) {
        stream << qint16(std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
    }

    if (!file.commit()) {
        return qcError(u"Cannot write %1"_s.arg(path));
    }
    return {};
}

QString describe(const QString &name, const QcSampleBuffer &buffer)
{
    float peak = 0.0f;
    for (const float sample : buffer.samples) {
        peak = std::max(peak, std::abs(sample));
    }
    return u"%1 %2 s peak %3"_s.arg(name).arg(buffer.seconds(), 0, 'f', 3).arg(peak, 0, 'f', 3);
}

} // namespace

namespace QcSoundExporter {

QcResult<QStringList> exportAll(const QcContentRegistry &content, const QString &directory, quint64 musicSeed)
{
    if (!QDir().mkpath(directory)) {
        return qcError(u"Cannot create %1"_s.arg(directory));
    }

    QStringList report;
    for (const QcSoundDefinition &sound : content.sounds()) {
        const QcSampleBuffer buffer = QcSoundSynthesizer::render(sound, 0);
        QString file = sound.name;
        file.replace(u':', u'_').replace(u'/', u'_');
        if (const QcStatus written = writeWav(QDir(directory).filePath(file + u".wav"_s), buffer); !written) {
            return qcError(written.error());
        }
        report.append(describe(sound.name, buffer));
    }

    const QcSampleBuffer music =
        QcMusicComposer::render(QcMusicComposer::compose(musicSeed), QcSoundSynthesizer::defaultSampleRate);
    const QString musicName = u"music-%1"_s.arg(musicSeed);
    if (const QcStatus written = writeWav(QDir(directory).filePath(musicName + u".wav"_s), music); !written) {
        return qcError(written.error());
    }
    report.append(describe(musicName, music));
    return report;
}

} // namespace QcSoundExporter

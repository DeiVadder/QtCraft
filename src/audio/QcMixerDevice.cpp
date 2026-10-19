#include "audio/QcMixerDevice.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// A pull-mode sink asks for at most what is available; this is far more than it ever buffers.
constexpr qint64 alwaysAvailableBytes = qint64(1) << 20;

} // namespace

QcMixerDevice::QcMixerDevice(Render render, Format format, QObject *parent)
    : QIODevice(parent)
    , m_render(std::move(render))
    , m_format(format)
{
}

qint64 QcMixerDevice::bytesAvailable() const
{
    return alwaysAvailableBytes + QIODevice::bytesAvailable();
}

int QcMixerDevice::bytesPerFrame() const
{
    return 2 * (m_format == Format::Float ? int(sizeof(float)) : int(sizeof(qint16)));
}

qint64 QcMixerDevice::readData(char *data, qint64 maxSize)
{
    const auto frames = int(std::min<qint64>(maxSize / bytesPerFrame(), alwaysAvailableBytes));
    if (frames <= 0) {
        return 0;
    }

    const std::span<const float> mixed = m_render(frames);
    if (m_format == Format::Float) {
        std::memcpy(data, mixed.data(), mixed.size() * sizeof(float));
    } else {
        for (std::size_t i = 0; i < mixed.size(); ++i) {
            const auto value = qint16(std::lround(std::clamp(mixed[i], -1.0f, 1.0f) * 32767.0f));
            std::memcpy(data + i * sizeof(qint16), &value, sizeof(qint16));
        }
    }
    return qint64(frames) * bytesPerFrame();
}

qint64 QcMixerDevice::writeData(const char *, qint64)
{
    return -1;
}

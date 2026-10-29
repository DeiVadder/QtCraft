// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/QcMixerDevice.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// A pull-mode sink asks for at most what is available; this is far more than it ever buffers.
constexpr qint64 alwaysAvailableBytes = qint64{1} << 20;

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
    return 2 * (m_format == Format::Float ? static_cast<int>(sizeof(float)) : static_cast<int>(sizeof(qint16)));
}

qint64 QcMixerDevice::readData(char *data, qint64 maxSize)
{
    const auto frames = static_cast<int>(std::min<qint64>(maxSize / bytesPerFrame(), alwaysAvailableBytes));
    if (frames <= 0) {
        return 0;
    }

    const std::span<const float> mixed = m_render(frames);
    if (m_format == Format::Float) {
        std::memcpy(data, mixed.data(), mixed.size() * sizeof(float));
    } else {
        for (std::size_t i = 0; i < mixed.size(); ++i) {
            const auto value = static_cast<qint16>(std::lround(std::clamp(mixed[i], -1.0f, 1.0f) * 32767.0f));
            std::memcpy(data + i * sizeof(qint16), &value, sizeof(qint16));
        }
    }
    return static_cast<qint64>(frames) * bytesPerFrame();
}

qint64 QcMixerDevice::writeData(const char *, qint64)
{
    return -1;
}

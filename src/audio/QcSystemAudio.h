// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "audio/QcAudioPlatform.h"

#include <QThread>

#include <memory>

class QAudioDevice;
class QMediaDevices;
class QcAudioSinkHost;

// The system's audio through Qt Multimedia: devices from QMediaDevices, and the sink on an audio
// thread of its own (QcAudioSinkHost), so a stalled GUI thread does not interrupt the sound. Lives
// in the GUI thread; open() and close() wait for the audio thread.
class QcSystemAudio : public QcAudioPlatform
{
    Q_OBJECT

public:
    // Audio the sink keeps buffered: short enough for direct feedback, long enough to ride out a
    // slow frame.
    static constexpr int bufferMilliseconds = 80;

    explicit QcSystemAudio(QObject *parent = nullptr);
    ~QcSystemAudio() override;
    Q_DISABLE_COPY_MOVE(QcSystemAudio)

    [[nodiscard]] QList<Device> outputs() const override;
    [[nodiscard]] QByteArray defaultOutput() const override;
    [[nodiscard]] std::optional<Format> format(const QByteArray &device, int keepRate) const override;
    [[nodiscard]] Opened open(const QByteArray &device, const Format &format, const QcMixerDevice::Render &render) override;
    void close() override;
    void watchOutputs(bool watch) override;

private:
    // Starts the audio thread with its sink host, once.
    void startThread();

    QThread m_thread;
    // Lives on m_thread from startThread() on; deleted after the thread stopped.
    std::unique_ptr<QcAudioSinkHost> m_host;
    std::unique_ptr<QMediaDevices> m_devices;
};

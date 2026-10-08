#pragma once

#include <QIODevice>

#include <functional>
#include <vector>

// Read-only device an audio sink pulls from: every read renders fresh interleaved stereo audio
// through `render`, converted to the sink's sample format. It never runs dry.
class QcMixerDevice : public QIODevice
{
    Q_OBJECT

public:
    enum class Format { Float, Int16 };
    using Render = std::function<void(float *out, int frames)>;

    QcMixerDevice(Render render, Format format, QObject *parent = nullptr);

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override;

protected:
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *data, qint64 maxSize) override;

private:
    int bytesPerFrame() const;

    Render m_render;
    Format m_format;
    std::vector<float> m_scratch;
};

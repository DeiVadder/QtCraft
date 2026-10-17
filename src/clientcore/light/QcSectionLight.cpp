#include "clientcore/light/QcSectionLight.h"

namespace {

// A channel with differing cells keeps them; a uniform one is reduced to its level.
void reduce(std::shared_ptr<const QcLightNibbles> &cells, quint8 &uniform)
{
    if (!cells) {
        return;
    }

    const int level = cells->uniformLevel();
    if (level >= 0) {
        uniform = quint8(level);
        cells.reset();
    }
}

} // namespace

QByteArray QcSectionLight::texels() const
{
    QByteArray result(qsizetype(QcLightNibbles::cellCount) * texelBytes, Qt::Uninitialized);
    auto *out = reinterpret_cast<quint8 *>(result.data());
    for (int index = 0; index < QcLightNibbles::cellCount; ++index) {
        const qsizetype at = qsizetype(index) * texelBytes;
        out[at] = quint8(blockLevel(index) * 17);
        out[at + 1] = quint8(skyLevel(index) * 17);
    }
    return result;
}

QcSectionLight QcSectionLight::fromChannels(std::shared_ptr<const QcLightNibbles> block, std::shared_ptr<const QcLightNibbles> sky)
{
    QcSectionLight light;
    light.block = std::move(block);
    light.sky = std::move(sky);
    reduce(light.block, light.uniformBlock);
    reduce(light.sky, light.uniformSky);
    return light;
}

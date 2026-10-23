#include "clientcore/light/QcSectionLight.h"

namespace {

// One channel of a section: its cells, or null when all cells have the level `uniform`.
struct ReducedChannel
{
    std::shared_ptr<const QcLightNibbles> cells;
    quint8 uniform = 0;
};

// A channel with differing cells keeps them; a uniform one is reduced to its level.
[[nodiscard]] ReducedChannel reduced(std::shared_ptr<const QcLightNibbles> cells, quint8 uniform)
{
    const int level = cells ? cells->uniformLevel() : -1;
    if (level >= 0) {
        return {nullptr, static_cast<quint8>(level)};
    }
    return {std::move(cells), uniform};
}

} // namespace

QByteArray QcSectionLight::texels() const
{
    QByteArray result(static_cast<qsizetype>(QcLightNibbles::cellCount) * texelBytes, Qt::Uninitialized);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): byte view of the QByteArray storage
    auto *out = reinterpret_cast<quint8 *>(result.data());
    for (int index = 0; index < QcLightNibbles::cellCount; ++index) {
        const qsizetype at = static_cast<qsizetype>(index) * texelBytes;
        out[at] = static_cast<quint8>(blockLevel(index) * 17);
        out[at + 1] = static_cast<quint8>(skyLevel(index) * 17);
    }
    return result;
}

QcSectionLight QcSectionLight::fromChannels(std::shared_ptr<const QcLightNibbles> block, std::shared_ptr<const QcLightNibbles> sky)
{
    QcSectionLight light;
    ReducedChannel blockChannel = reduced(std::move(block), light.uniformBlock);
    ReducedChannel skyChannel = reduced(std::move(sky), light.uniformSky);
    light.block = std::move(blockChannel.cells);
    light.uniformBlock = blockChannel.uniform;
    light.sky = std::move(skyChannel.cells);
    light.uniformSky = skyChannel.uniform;
    return light;
}

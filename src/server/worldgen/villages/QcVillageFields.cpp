#include "QcVillageFields.h"

#include <algorithm>

namespace QcVillageFields {

namespace {

bool isCrop(const QcStructure &structure, const QcContentRegistry &content, int x, int y, int z)
{
    const std::optional<QcBlockId> block = structure.block(x, y, z);
    return block && content.isValidBlock(*block) && content.block(*block).crop.isCrop();
}

const QcVillageStyle::Crop &chooseCrop(const QList<QcVillageStyle::Crop> &crops, double random)
{
    int total = 0;
    for (const QcVillageStyle::Crop &crop : crops) {
        total += crop.weight;
    }
    int pick = int(random * total);
    for (const QcVillageStyle::Crop &crop : crops) {
        if (pick < crop.weight) {
            return crop;
        }
        pick -= crop.weight;
    }
    return crops.back();
}

QcBlockId stageOf(const QcVillageStyle::Crop &crop, double random)
{
    const qsizetype ripe = crop.stages.size() - 1;
    if (random < ripeChance || ripe == 0) {
        return crop.stages[ripe];
    }
    const double growing = (random - ripeChance) / (1.0 - ripeChance);
    return crop.stages[std::min(ripe - 1, qsizetype(growing * double(ripe)))];
}

} // namespace

bool hasCrops(const QcStructure &structure, const QcContentRegistry &content)
{
    const int layer = structure.sizeX() * structure.sizeZ();
    for (int cell = 0; cell < layer * structure.sizeY(); ++cell) {
        if (isCrop(structure, content, cell % structure.sizeX(), cell / layer, cell % layer / structure.sizeX())) {
            return true;
        }
    }
    return false;
}

QList<QcBlockPos> cropCells(const QcStructure &structure, const QcContentRegistry &content)
{
    QList<QcBlockPos> cells;
    const int layer = structure.sizeX() * structure.sizeZ();
    for (int cell = 0; cell < layer * structure.sizeY(); ++cell) {
        const QcBlockPos local{cell % structure.sizeX(), cell / layer, cell % layer / structure.sizeX()};
        if (isCrop(structure, content, local.x, local.y, local.z)) {
            cells.append(local);
        }
    }
    return cells;
}

std::shared_ptr<const QcStructure> plant(const QcStructure &structure, const QList<QcVillageStyle::Crop> &crops,
                                         const QcContentRegistry &content, const std::function<double()> &random)
{
    auto planted = std::make_shared<QcStructure>(structure);
    // Cells in the order x, then z, then y: a row is a run of crop cells along x, and a new row
    // starts at every crop cell after a cell without one (or at the start of a line).
    const QcVillageStyle::Crop *row = nullptr;
    const int layer = structure.sizeX() * structure.sizeZ();
    for (int cell = 0; cell < layer * structure.sizeY(); ++cell) {
        const int x = cell % structure.sizeX();
        const int y = cell / layer;
        const int z = cell % layer / structure.sizeX();
        const bool crop = isCrop(structure, content, x, y, z);
        if (x == 0 || !crop) {
            row = nullptr;
        }
        if (!crop) {
            continue;
        }

        if (!row) {
            row = &chooseCrop(crops, random());
        }
        planted->set(x, y, z, stageOf(*row, random()));
    }
    return planted;
}

} // namespace QcVillageFields

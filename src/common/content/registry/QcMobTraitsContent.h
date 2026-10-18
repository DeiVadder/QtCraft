#pragma once

#include <QStringList>

class QcContentRegistry;
struct QcMobDefinition;

// Checks the traits of creatures (decision E-460) when the content is finished.
namespace QcMobTraitsContent {

// Resolves the blocks a creature carries (QcCarryDefinition::blockIds): a name without a block, or a
// block without an item of its own (it could not drop when the creature dies), with a block entity
// (storage, machine, sapling, circuit part) or of two cells (door, bed) is left out with a warning;
// a creature left without any carries nothing. Returns the warnings.
QStringList resolve(const QcContentRegistry &content, QcMobDefinition &mob);

} // namespace QcMobTraitsContent

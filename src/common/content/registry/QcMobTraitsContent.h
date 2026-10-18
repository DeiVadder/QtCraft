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
// Also resolves the kind a tamed one becomes (QcTameDefinition::becomesType; a name that is no
// tameable creature is ignored with a warning) and warns of unknown taming, healing and dye items.
QStringList resolve(const QcContentRegistry &content, QcMobDefinition &mob);
// The taming part of resolve (decision E-480).
QStringList resolveTame(const QcContentRegistry &content, QcMobDefinition &mob);
// Warns of structure creatures with an owner (QcStructureDefinition::Mob::owner) of a kind that is not
// tameable: they are placed wild.
QStringList checkStructurePets(const QcContentRegistry &content);

} // namespace QcMobTraitsContent

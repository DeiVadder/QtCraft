#pragma once

#include <QList>
#include <QString>

class QcContentRegistry;

// The stonecutter's recipes (decision E-784). Packs define them in `stonecutting/` like smelting
// recipes; generate() adds those of building parts: a part family with a `stonecutter` count cuts
// the base block of every material of its `stonecutter` kinds into that many parts (stone into two
// stone slabs), and a block cut one to one into the base block of a material is cut into that
// material's parts too (stone into stone brick stairs). A recipe a pack defines under the same name
// wins over the generated one.
namespace QcStonecutting {

void generate(QcContentRegistry &registry);

// The indices of the stonecutting recipes that take `inputItem`, in the order of the registry.
[[nodiscard]] QList<qsizetype> recipesFor(const QcContentRegistry &content, const QString &inputItem);

} // namespace QcStonecutting

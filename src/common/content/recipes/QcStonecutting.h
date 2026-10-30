// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcItemStack.h"

#include <QList>
#include <QString>

class QcContentRegistry;

/*! \brief The stonecutter's recipes (decision E-784).

    Packs define them in \c {stonecutting/} like smelting recipes; generate() adds those of
    building parts: a part family with a \c {stonecutter} count cuts the base block of every
    material of its \c {stonecutter} kinds into that many parts (stone into two stone slabs), and
    a block cut one to one into the base block of a material is cut into that material's parts
    too (stone into stone brick stairs). A recipe a pack defines under the same name wins over
    the generated one; a generated name longer than maxRecipeName is skipped with a warning. */
namespace QcStonecutting {

/*! The longest stonecutting recipe name the protocol carries (StonecutterState,
    SelectStonecutterRecipe); generate() skips a recipe whose name would be longer. */
constexpr qsizetype maxRecipeName = 256;

/*! Registers in \a registry the generated recipes of building parts (see above), named
    \c {<part>_from_<input path>} (\c {<part>_from_<namespace>:<path>} when the input comes
    from another pack); a name a pack already uses, or an unknown input or part item, is
    skipped. */
void generate(QcContentRegistry &registry);

/*! Returns the indices of the stonecutting recipes of \a content that take \a inputItem, in the
    order of the registry. */
[[nodiscard]] QList<qsizetype> recipesFor(const QcContentRegistry &content, const QString &inputItem);

/*! Returns the stonecutter's output slot (decision E-785): what the recipe of \a content named
    \a recipe makes from one item of \a input, or an empty stack when the input is empty, the
    recipe unknown or for another input. */
[[nodiscard]] QcItemStack output(const QcContentRegistry &content, const QcItemStack &input, const QString &recipe);

} // namespace QcStonecutting

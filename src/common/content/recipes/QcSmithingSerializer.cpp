// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/recipes/QcSmithingSerializer.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/protocol/QcStreamLimits.h"

#include <optional>

using namespace Qt::StringLiterals;

namespace {

// One recipe as write() puts it, or nothing when the stream is corrupt.
std::optional<QcSmithingRecipe> readRecipe(QDataStream &stream)
{
    QcSmithingRecipe recipe;
    stream >> recipe.name >> recipe.templateItem >> recipe.base >> recipe.addition >> recipe.result;
    const bool fits = stream.status() == QDataStream::Ok && !recipe.name.isEmpty();
    return fits ? std::optional{std::move(recipe)} : std::nullopt;
}

} // namespace

namespace QcSmithingSerializer {

void write(QDataStream &stream, const QList<QcSmithingRecipe> &recipes)
{
    stream << static_cast<quint32>(recipes.size());
    for (const QcSmithingRecipe &recipe : recipes) {
        stream << recipe.name << recipe.templateItem << recipe.base << recipe.addition << recipe.result;
    }
}

QcStatus read(QDataStream &stream, QcContentRegistry &registry)
{
    const std::optional<quint32> recipes = QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions);
    if (!recipes) {
        return qcError(u"Corrupt content: smithing recipe table"_s);
    }
    for (quint32 index = 0; index < *recipes; ++index) {
        std::optional<QcSmithingRecipe> recipe = readRecipe(stream);
        if (!recipe) {
            return qcError(u"Corrupt content: smithing recipe %1"_s.arg(index));
        }
        registry.registerSmithingRecipe(std::move(*recipe));
    }
    return {};
}

} // namespace QcSmithingSerializer

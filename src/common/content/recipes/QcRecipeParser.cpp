// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/recipes/QcRecipeParser.h"

#include "common/content/recipes/QcStonecutting.h"
#include "common/content/registry/QcContentFields.h"
#include "common/content/registry/QcJsonFieldReader.h"
#include "common/core/QcLogging.h"

#include <QJsonArray>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

// The rows without blank rows at the top and bottom.
[[nodiscard]] QList<QString> withoutBlankRows(QList<QString> rows)
{
    while (!rows.isEmpty() && rows.first().trimmed().isEmpty()) {
        rows.removeFirst();
    }
    while (!rows.isEmpty() && rows.last().trimmed().isEmpty()) {
        rows.removeLast();
    }
    return rows;
}

// The rows padded to one width, without blank columns at both sides.
[[nodiscard]] QList<QString> withoutBlankColumns(QList<QString> rows)
{
    qsizetype width = 0;
    for (const QString &row : std::as_const(rows)) {
        width = std::max(width, row.size());
    }
    for (QString &row : rows) {
        row = row.leftJustified(width, u' ');
    }

    const auto columnBlank = [&rows](qsizetype column) {
        return std::ranges::all_of(rows, [column](const QString &row) { return row[column] == u' '; });
    };

    while (width > 0 && columnBlank(0)) {
        for (QString &row : rows) {
            row.remove(0, 1);
        }
        --width;
    }

    while (width > 0 && columnBlank(width - 1)) {
        for (QString &row : rows) {
            row.chop(1);
        }
        --width;
    }
    return rows;
}

[[nodiscard]] decltype(QcRecipeDefinition::keys) parsePatternKeys(const QJsonObject &json, QcJsonFieldReader &reader,
                                                                 const QString &packNamespace)
{
    decltype(QcRecipeDefinition::keys) result;
    const QJsonObject keys = json.value(u"key"_s).toObject();
    if (keys.size() > 9) {
        reader.fail(u"'key' has more than 9 entries"_s);
    }

    for (auto it = keys.constBegin(); it != keys.constEnd(); ++it) {
        const QString item = QcContentFields::qualifyIngredient(it.value().toString(), packNamespace);
        if (it.key().size() != 1 || it.key() == u" "_s || item.isEmpty()) {
            reader.fail(u"'key' maps single characters to item names"_s);
            break;
        }
        result.append({it.key().at(0), item});
    }
    return result;
}

void checkPatternUsesKeys(const QcRecipeDefinition &recipe, QcJsonFieldReader &reader)
{
    for (const QString &row : std::as_const(recipe.pattern)) {
        for (const QChar c : row) {
            if (c != u' ' && std::ranges::none_of(recipe.keys, [c](const auto &key) { return key.first == c; })) {
                reader.fail(u"pattern uses '%1' without a key"_s.arg(c));
            }
        }
    }
}

// The rows of "pattern", trimmed.
[[nodiscard]] QList<QString> parsePattern(const QJsonObject &json, QcJsonFieldReader &reader)
{
    QList<QString> rows;
    const QJsonArray pattern = json.value(u"pattern"_s).toArray();
    for (const auto &row : pattern) {
        rows.append(row.toString());
    }

    // Trim blank rows and columns so the shape can sit anywhere in the grid.
    rows = withoutBlankColumns(withoutBlankRows(std::move(rows)));
    const qsizetype width = rows.isEmpty() ? 0 : rows.first().size();
    if (rows.isEmpty() || rows.size() > 3 || width > 3) {
        reader.fail(u"'pattern' must have 1 to 3 rows of at most 3 keys"_s);
    }
    return rows;
}

[[nodiscard]] QStringList parseIngredients(const QJsonObject &json, QcJsonFieldReader &reader, const QString &packNamespace)
{
    QStringList result;
    const QJsonArray ingredients = json.value(u"ingredients"_s).toArray();
    for (const auto &entry : ingredients) {
        const QString item = QcContentFields::qualifyIngredient(entry.toString(), packNamespace);
        if (item.isEmpty()) {
            reader.fail(u"invalid ingredient"_s);
            break;
        }
        result.append(item);
    }

    if (result.isEmpty() || result.size() > 9) {
        reader.fail(u"'ingredients' needs 1 to 9 items"_s);
    }
    return result;
}

} // namespace

namespace QcRecipeParser {

[[nodiscard]] QcResult<QcRecipeDefinition> parseRecipe(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"recipe"_s, {}};
    QcRecipeDefinition recipe;
    recipe.name = reader.name(u"name"_s, packNamespace);
    if (recipe.name.isEmpty()) {
        return qcError(reader.ok() ? u"recipe: 'name' is required"_s : reader.error);
    }

    reader.context = u"recipe '%1'"_s.arg(recipe.name);
    const QString type = reader.string(u"type"_s, u"shaped"_s);
    recipe.shaped = type == u"shaped"_s;
    if (!recipe.shaped && type != u"shapeless"_s) {
        reader.fail(u"'type' must be shaped or shapeless"_s);
    }

    const QJsonObject result = json.value(u"result"_s).toObject();
    QcJsonFieldReader resultReader{result, reader.context + u" result"_s, {}};
    recipe.resultItem = resultReader.name(u"item"_s, packNamespace);
    recipe.resultCount = resultReader.integer(u"count"_s, 1, 1, 64);
    if (!resultReader.ok() || recipe.resultItem.isEmpty()) {
        reader.fail(resultReader.ok() ? u"'result' needs an 'item'"_s : resultReader.error);
    }

    if (recipe.shaped) {
        recipe.pattern = parsePattern(json, reader);
        recipe.keys = parsePatternKeys(json, reader, packNamespace);
        checkPatternUsesKeys(recipe, reader);
    } else {
        recipe.ingredients = parseIngredients(json, reader, packNamespace);
    }

    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return recipe;
}

[[nodiscard]] QcResult<QcSmeltingRecipe> parseSmeltingRecipe(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"smelting recipe"_s, {}};
    QcSmeltingRecipe recipe;
    recipe.name = reader.name(u"name"_s, packNamespace);
    if (recipe.name.isEmpty()) {
        return qcError(reader.ok() ? u"smelting recipe: 'name' is required"_s : reader.error);
    }

    reader.context = u"smelting recipe '%1'"_s.arg(recipe.name);
    recipe.category = reader.string(u"category"_s, recipe.category);
    if (reader.ok() && !QcContentFields::isValidCategory(recipe.category)) {
        reader.fail(QcContentFields::invalidCategoryMessage());
    }
    // The stonecutter's recipes have their own folder and list (decision E-784).
    if (reader.ok() && recipe.category == QLatin1StringView(QcSmeltingRecipe::stonecuttingCategory)) {
        reader.fail(u"the category 'stonecutting' belongs to recipes in stonecutting/"_s);
    }
    recipe.kind = reader.string(u"kind"_s);
    if (reader.ok() && !recipe.kind.isEmpty() && !QcContentFields::isValidCategory(recipe.kind)) {
        reader.fail(u"'kind' must be a name like a category"_s);
    }
    recipe.inputItem = reader.name(u"input"_s, packNamespace);
    if (reader.ok() && recipe.inputItem.isEmpty()) {
        reader.fail(u"'input' is required"_s);
    }

    recipe.seconds = reader.number(u"seconds"_s, recipe.seconds, QcSmeltingRecipe::minSeconds, QcSmeltingRecipe::maxSeconds);
    recipe.experience = reader.number(u"experience"_s, 0.0, 0.0, QcSmeltingRecipe::maxExperience);
    const QJsonValue result = json.value(u"result"_s);
    const QJsonObject resultJson = result.toObject();
    QcJsonFieldReader resultReader{resultJson, reader.context + u" result"_s, {}};
    recipe.resultItem = resultReader.name(u"item"_s, packNamespace);
    recipe.resultCount = resultReader.integer(u"count"_s, 1, 1, 64);
    if (!result.isObject() || !resultReader.ok() || recipe.resultItem.isEmpty()) {
        reader.fail(resultReader.ok() ? u"'result' needs an 'item'"_s : resultReader.error);
    }

    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return recipe;
}

[[nodiscard]] QcResult<QcSmeltingRecipe> parseStonecuttingRecipe(const QJsonObject &json, const QString &packNamespace)
{
    QJsonObject smelting = json;
    smelting.remove(u"category"_s);
    QcResult<QcSmeltingRecipe> recipe = parseSmeltingRecipe(smelting, packNamespace);
    // A longer name would not fit into the protocol's stonecutter messages.
    if (recipe && recipe->name.size() > QcStonecutting::maxRecipeName) {
        qCWarning(lcRegistry) << "Stonecutting recipe" << recipe->name << "skipped: its name is longer than" << QcStonecutting::maxRecipeName
                              << "characters";
        return qcError(u"the name is longer than %1 characters"_s.arg(QcStonecutting::maxRecipeName));
    }
    if (recipe) {
        recipe->category = QString::fromLatin1(QcSmeltingRecipe::stonecuttingCategory);
    }
    return recipe;
}

} // namespace QcRecipeParser

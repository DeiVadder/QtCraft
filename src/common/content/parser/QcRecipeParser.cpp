#include "QcRecipeParser.h"

#include "QcContentFields.h"
#include "QcJsonFieldReader.h"

#include <QJsonArray>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

void trimBlankRows(QList<QString> &rows)
{
    while (!rows.isEmpty() && rows.first().trimmed().isEmpty()) {
        rows.removeFirst();
    }
    while (!rows.isEmpty() && rows.last().trimmed().isEmpty()) {
        rows.removeLast();
    }
}

// Pads the rows to one width, removes blank columns at both sides and returns the remaining width.
qsizetype trimBlankColumns(QList<QString> &rows)
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
    return width;
}

void parsePatternKeys(const QJsonObject &json, QcRecipeDefinition &recipe, QcJsonFieldReader &reader, const QString &packNamespace)
{
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
        recipe.keys.append({it.key().at(0), item});
    }
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

void parseShaped(const QJsonObject &json, QcRecipeDefinition &recipe, QcJsonFieldReader &reader, const QString &packNamespace)
{
    QList<QString> rows;
    const QJsonArray pattern = json.value(u"pattern"_s).toArray();
    for (const auto &row : pattern) {
        rows.append(row.toString());
    }

    // Trim blank rows and columns so the shape can sit anywhere in the grid.
    trimBlankRows(rows);
    const qsizetype width = trimBlankColumns(rows);
    if (rows.isEmpty() || rows.size() > 3 || width > 3) {
        reader.fail(u"'pattern' must have 1 to 3 rows of at most 3 keys"_s);
    }

    recipe.pattern = rows;
    parsePatternKeys(json, recipe, reader, packNamespace);
    checkPatternUsesKeys(recipe, reader);
}

void parseShapeless(const QJsonObject &json, QcRecipeDefinition &recipe, QcJsonFieldReader &reader, const QString &packNamespace)
{
    const QJsonArray ingredients = json.value(u"ingredients"_s).toArray();
    for (const auto &entry : ingredients) {
        const QString item = QcContentFields::qualifyIngredient(entry.toString(), packNamespace);
        if (item.isEmpty()) {
            reader.fail(u"invalid ingredient"_s);
            break;
        }
        recipe.ingredients.append(item);
    }

    if (recipe.ingredients.isEmpty() || recipe.ingredients.size() > 9) {
        reader.fail(u"'ingredients' needs 1 to 9 items"_s);
    }
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
        parseShaped(json, recipe, reader, packNamespace);
    } else {
        parseShapeless(json, recipe, reader, packNamespace);
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
    recipe.inputItem = reader.name(u"input"_s, packNamespace);
    if (reader.ok() && recipe.inputItem.isEmpty()) {
        reader.fail(u"'input' is required"_s);
    }

    recipe.seconds = reader.number(u"seconds"_s, recipe.seconds, QcSmeltingRecipe::minSeconds, QcSmeltingRecipe::maxSeconds);
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

} // namespace QcRecipeParser

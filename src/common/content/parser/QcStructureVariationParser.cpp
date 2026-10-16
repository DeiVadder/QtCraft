#include "QcStructureVariationParser.h"

#include "QcContentNames.h"

#include <QJsonArray>

#include <optional>

using namespace Qt::StringLiterals;

namespace {

using Structure = QcStructureDefinition;

// One option of a choice: {"oak_planks": "birch_planks", ...}, names qualified.
[[nodiscard]] std::optional<QHash<QString, QString>> readOption(const QJsonValue &value, const QString &packNamespace)
{
    if (!value.isObject() || value.toObject().size() > Structure::maxPaletteEntries) {
        return std::nullopt;
    }

    QHash<QString, QString> swaps;
    const QJsonObject object = value.toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        const QString from = QcContentNames::qualify(it.key(), packNamespace);
        const QString to = it.value().isString() ? QcContentNames::qualify(it.value().toString(), packNamespace) : QString();
        if (from.isEmpty() || to.isEmpty()) {
            return std::nullopt;
        }
        swaps.insert(from, to);
    }
    return swaps;
}

void parseChoices(const QJsonObject &json, Structure &structure, QcJsonFieldReader &reader, const QString &packNamespace)
{
    const QJsonValue value = json.value(u"choices"_s);
    if (value.isUndefined()) {
        return;
    }
    if (!value.isObject() || value.toObject().size() > Structure::maxChoices) {
        reader.fail(u"'choices' must be an object of at most %1 choices"_s.arg(Structure::maxChoices));
        return;
    }

    const QJsonObject choices = value.toObject();
    for (auto it = choices.constBegin(); it != choices.constEnd(); ++it) {
        const QJsonArray options = it.value().toArray();
        if (!it.value().isArray() || options.isEmpty() || options.size() > Structure::maxOptions) {
            reader.fail(u"choice '%1' must be an array of 1 to %2 options"_s.arg(it.key()).arg(Structure::maxOptions));
            return;
        }

        Structure::Choice choice{it.key(), {}};
        for (const auto &option : options) {
            std::optional<QHash<QString, QString>> swaps = readOption(option, packNamespace);
            if (!swaps) {
                reader.fail(u"every option of choice '%1' must be an object of name -> name"_s.arg(it.key()));
                return;
            }
            choice.options.append(std::move(*swaps));
        }
        structure.choices.append(std::move(choice));
    }
}

// What reading one part needs: the palette keys, the structure, the part and the reader.
struct PartReading
{
    const QHash<QChar, qint16> &keys;
    const Structure &structure;
    Structure::Part &part;
    QcJsonFieldReader &reader;
};

// The cell index of `cell` in the structure; std::nullopt where it lies outside.
[[nodiscard]] std::optional<qsizetype> cellIndexOf(QcBlockPos cell, const Structure &structure)
{
    if (cell.x < 0 || cell.y < 0 || cell.z < 0 || cell.x >= structure.sizeX || cell.y >= structure.sizeY || cell.z >= structure.sizeZ) {
        return std::nullopt;
    }
    return structure.cellIndex(cell.x, cell.y, cell.z);
}

// One row of a part starting at `start` in the structure; false (with the reader failed) when it
// uses an unknown key or leaves the structure.
bool readPartRow(const QString &row, QcBlockPos start, PartReading &reading)
{
    for (int x = 0; x < row.size(); ++x) {
        if (row[x] == u' ') {
            continue;
        }
        const std::optional<qsizetype> cell = cellIndexOf(start.offset(x, 0, 0), reading.structure);
        if (!reading.keys.contains(row[x]) || !cell) {
            reading.reader.fail(u"part '%1' uses '%2' outside the structure or not in the palette"_s.arg(reading.part.name).arg(row[x]));
            return false;
        }
        reading.part.cells[*cell] = reading.keys.value(row[x]);
    }
    return true;
}

// The layers of one part, written into its cells with their north west bottom corner at `at`.
void readPartLayers(const QJsonArray &layers, QcBlockPos at, PartReading &reading)
{
    for (int y = 0; y < layers.size(); ++y) {
        const QJsonArray rows = layers[y].toArray();
        for (int z = 0; z < rows.size(); ++z) {
            if (!readPartRow(rows[z].toString(), at.offset(0, y, z), reading)) {
                return;
            }
        }
    }
}

void parseParts(const QJsonObject &json, const QHash<QChar, qint16> &keys, Structure &structure, QcJsonFieldReader &reader)
{
    const QJsonValue value = json.value(u"parts"_s);
    if (value.isUndefined()) {
        return;
    }
    const QJsonArray parts = value.toArray();
    if (!value.isArray() || parts.size() > Structure::maxParts) {
        reader.fail(u"'parts' must be an array of at most %1 parts"_s.arg(Structure::maxParts));
        return;
    }

    for (const auto &entry : parts) {
        QcJsonFieldReader nested{entry.toObject(), reader.context, {}};
        Structure::Part part;
        part.name = nested.string(u"name"_s);
        part.chance = nested.number(u"chance"_s, 0.5, 0.0, 1.0);
        const QJsonArray at = entry.toObject().value(u"at"_s).toArray();
        const QJsonValue layers = entry.toObject().value(u"layers"_s);
        if (!entry.isObject() || at.size() != 3 || !layers.isArray()) {
            nested.fail(u"every part needs 'at' ([x, y, z]) and 'layers'"_s);
        }
        part.cells.fill(Structure::keep, structure.cells.size());
        const QcBlockPos corner{at[0].toInt(), at[1].toInt(), at[2].toInt()};
        if (nested.ok()) {
            PartReading reading{keys, structure, part, nested};
            readPartLayers(layers.toArray(), corner, reading);
        }
        reader.adopt(nested);
        if (!reader.ok()) {
            return;
        }
        structure.parts.append(std::move(part));
    }
}

} // namespace

namespace QcStructureVariationParser {

void parseVariation(const QJsonObject &json, const QHash<QChar, qint16> &keys, QcStructureDefinition &structure, QcJsonFieldReader &reader,
                    const QString &packNamespace)
{
    parseChoices(json, structure, reader, packNamespace);
    if (reader.ok()) {
        parseParts(json, keys, structure, reader);
    }
}

} // namespace QcStructureVariationParser

#include "common/content/parser/QcStructureVariationParser.h"

#include "common/content/registry/QcContentNames.h"

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

// The cell index of `cell` in the structure; std::nullopt where it lies outside.
[[nodiscard]] std::optional<qsizetype> cellIndexOf(QcBlockPos cell, const Structure &structure)
{
    if (cell.x < 0 || cell.y < 0 || cell.z < 0 || cell.x >= structure.sizeX || cell.y >= structure.sizeY || cell.z >= structure.sizeZ) {
        return std::nullopt;
    }
    return structure.cellIndex(cell.x, cell.y, cell.z);
}

// Reads the layers of one part: owns the part being built, whose cells span the whole structure.
class PartReader
{
public:
    PartReader(const QHash<QChar, qint16> &keys, const Structure &structure, Structure::Part part)
        : m_keys(keys)
        , m_structure(structure)
        , m_part(std::move(part))
    {
        m_part.cells.fill(Structure::keep, structure.cells.size());
    }

    // The layers of the part, written into its cells with their north west bottom corner at `at`.
    void readLayers(const QJsonArray &layers, QcBlockPos at, QcJsonFieldReader &reader)
    {
        for (int y = 0; y < layers.size(); ++y) {
            const QJsonArray rows = layers[y].toArray();
            for (int z = 0; z < rows.size(); ++z) {
                if (!readRow(rows[z].toString(), at.offset(0, y, z), reader)) {
                    return;
                }
            }
        }
    }

    [[nodiscard]] Structure::Part take() { return std::move(m_part); }

private:
    // One row of the part starting at `start` in the structure; false (with the reader failed) when
    // it uses an unknown key or leaves the structure.
    bool readRow(const QString &row, QcBlockPos start, QcJsonFieldReader &reader)
    {
        for (int x = 0; x < row.size(); ++x) {
            if (row[x] == u' ') {
                continue;
            }
            const std::optional<qsizetype> cell = cellIndexOf(start.offset(x, 0, 0), m_structure);
            if (!m_keys.contains(row[x]) || !cell) {
                reader.fail(u"part '%1' uses '%2' outside the structure or not in the palette"_s.arg(m_part.name).arg(row[x]));
                return false;
            }
            m_part.cells[*cell] = m_keys.value(row[x]);
        }
        return true;
    }

    const QHash<QChar, qint16> &m_keys;
    const Structure &m_structure;
    Structure::Part m_part;
};

} // namespace

namespace QcStructureVariationParser {

QList<QcStructureDefinition::Choice> parseChoices(const QJsonObject &json, QcJsonFieldReader &reader, const QString &packNamespace)
{
    QList<Structure::Choice> result;
    const QJsonValue value = json.value(u"choices"_s);
    if (value.isUndefined()) {
        return result;
    }
    if (!value.isObject() || value.toObject().size() > Structure::maxChoices) {
        reader.fail(u"'choices' must be an object of at most %1 choices"_s.arg(Structure::maxChoices));
        return result;
    }

    const QJsonObject choices = value.toObject();
    for (auto it = choices.constBegin(); it != choices.constEnd(); ++it) {
        const QJsonArray options = it.value().toArray();
        if (!it.value().isArray() || options.isEmpty() || options.size() > Structure::maxOptions) {
            reader.fail(u"choice '%1' must be an array of 1 to %2 options"_s.arg(it.key()).arg(Structure::maxOptions));
            return result;
        }

        Structure::Choice choice{it.key(), {}};
        for (const auto &option : options) {
            std::optional<QHash<QString, QString>> swaps = readOption(option, packNamespace);
            if (!swaps) {
                reader.fail(u"every option of choice '%1' must be an object of name -> name"_s.arg(it.key()));
                return result;
            }
            choice.options.append(std::move(*swaps));
        }
        result.append(std::move(choice));
    }
    return result;
}

QList<QcStructureDefinition::Part> parseParts(const QJsonObject &json, const QHash<QChar, qint16> &keys, const QcStructureDefinition &structure,
                                              QcJsonFieldReader &reader)
{
    QList<Structure::Part> result;
    const QJsonValue value = json.value(u"parts"_s);
    if (value.isUndefined()) {
        return result;
    }
    const QJsonArray parts = value.toArray();
    if (!value.isArray() || parts.size() > Structure::maxParts) {
        reader.fail(u"'parts' must be an array of at most %1 parts"_s.arg(Structure::maxParts));
        return result;
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
        PartReader partReader(keys, structure, std::move(part));
        const QcBlockPos corner{at[0].toInt(), at[1].toInt(), at[2].toInt()};
        if (nested.ok()) {
            partReader.readLayers(layers.toArray(), corner, nested);
        }
        reader.adopt(nested);
        if (!reader.ok()) {
            return result;
        }
        result.append(partReader.take());
    }
    return result;
}

} // namespace QcStructureVariationParser

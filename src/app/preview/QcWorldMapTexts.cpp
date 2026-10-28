#include "app/preview/QcWorldMapTexts.h"

#include "app/preview/QcWorldMapPainter.h"
#include "app/preview/QcWorldMapWork.h"
#include "common/content/definitions/QcStructureDefinition.h"
#include "common/content/registry/QcContentRegistry.h"

#include <QColor>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

QVariantMap legendEntry(const QString &name, quint32 colour, double share)
{
    return {{u"name"_s, name}, {u"colour"_s, QColor::fromRgb(colour)}, {u"share"_s, share}, {u"percent"_s, static_cast<int>(std::lround(share * 100.0))}};
}

} // namespace

QcWorldMapTexts::QcWorldMapTexts(const QcContentRegistry *content, QString language)
    : m_content(content)
    , m_language(std::move(language))
{
}

QString QcWorldMapTexts::biome(int index) const
{
    if (!m_content || index < 0 || index >= m_content->worldgen().biomes().size()) {
        return {};
    }
    const QcBiomeDefinition &definition = m_content->worldgen().biomes().at(index);
    const QString name = text(definition.translationKey);
    return name.isEmpty() ? definition.name : name;
}

QString QcWorldMapTexts::kind(const QString &kind) const
{
    if (kind == u"village"_s) {
        return tr("Village");
    }
    // A structure of the content ("qtcraft:desert_temple"; the coastal tower is named without
    // its pack).
    const QString name = kind.contains(u':') ? kind : u"qtcraft:"_s + kind;
    const QcStructureDefinition *structure = m_content ? m_content->worldgen().structure(name) : nullptr;
    const QString title = structure ? text(structure->translationKey) : QString();
    return title.isEmpty() ? kind.section(u':', -1) : title;
}

QString QcWorldMapTexts::cell(const QcWorldMapResult &map, int cellX, int cellZ) const
{
    const int size = map.area.cells();
    if (cellX < 0 || cellZ < 0 || cellX >= size || cellZ >= size || map.cells.size() != static_cast<qsizetype>(size) * size) {
        return {};
    }
    const qint16 code = map.cells.at(static_cast<qsizetype>(cellZ) * size + cellX);
    if (code == QcWorldMapResult::seaCell) {
        return tr("Sea and lakes");
    }
    if (code == QcWorldMapResult::iceCell) {
        return tr("Frozen sea and lakes");
    }
    return biome(code);
}

QVariantList QcWorldMapTexts::legend(const QcWorldMapResult &map) const
{
    const qsizetype biomes = m_content ? m_content->worldgen().biomes().size() : 0;
    const auto cells = static_cast<double>(map.area.cells()) * map.area.cells();
    if (cells <= 0.0 || map.biomeCells.size() != biomes) {
        return {};
    }

    QList<std::pair<int, QVariantMap>> entries;
    for (qsizetype i = 0; i < biomes; ++i) {
        if (const int count = map.biomeCells.at(i); count > 0) {
            entries.append({count, legendEntry(biome(static_cast<int>(i)), map.biomeColours.at(i), count / cells)});
        }
    }
    if (map.seaCells > 0) {
        entries.append({map.seaCells, legendEntry(tr("Sea and lakes"), QcWorldMapPainter::seaColour(), map.seaCells / cells)});
    }
    if (map.iceCells > 0) {
        entries.append({map.iceCells, legendEntry(tr("Frozen sea and lakes"), QcWorldMapPainter::iceColour(), map.iceCells / cells)});
    }
    std::ranges::stable_sort(entries, std::greater{}, &std::pair<int, QVariantMap>::first);
    QVariantList legend;
    for (const auto &entry : std::as_const(entries)) {
        legend.append(entry.second);
    }
    return legend;
}

QString QcWorldMapTexts::place(const QString &name, int x, int z)
{
    return tr("%1, x %2, z %3").arg(name).arg(x).arg(z);
}

QString QcWorldMapTexts::text(const QString &key) const
{
    const QcTranslations &translations = m_content->translations();
    return translations.has(key, m_language) || translations.has(key, u"en"_s) ? translations.translate(key, m_language) : QString();
}

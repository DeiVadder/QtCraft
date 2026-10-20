#include "common/content/parser/QcHangingParser.h"

#include "common/content/parser/QcJsonFieldReader.h"
#include "common/content/registry/QcAssetCatalog.h"

#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace {

QcPaintingMotif parseMotif(const QJsonValue &value, const QString &packNamespace, QcJsonFieldReader &reader)
{
    if (!value.isObject()) {
        reader.fail(u"every motif must be an object"_s);
        return {};
    }

    const QJsonObject object = value.toObject();
    QcJsonFieldReader nested{object, reader.context + u" motif"_s, {}};
    QcPaintingMotif motif;
    motif.name = nested.string(u"name"_s);
    motif.texture = nested.name(u"texture"_s, packNamespace);
    motif.width = nested.integer(u"width"_s, 1, 1, QcPaintingMotif::maxSide);
    motif.height = nested.integer(u"height"_s, 1, 1, QcPaintingMotif::maxSide);
    if (nested.ok() && (motif.name.isEmpty() || motif.texture.isEmpty())) {
        nested.fail(u"needs a 'name' and a 'texture'"_s);
    }
    motif.nameTiles();
    reader.adopt(nested);
    return motif;
}

} // namespace

namespace QcHangingParser {

QcHangingDefinition parseHanging(const QJsonObject &json, const QString &packNamespace, QcJsonFieldReader &reader)
{
    QcHangingDefinition hanging;
    const QString type = reader.string(u"type"_s);
    if (type == u"painting"_s) {
        hanging.type = QcHangingType::Painting;
    } else if (type != u"item_frame"_s) {
        reader.fail(u"'type' must be \"item_frame\" or \"painting\""_s);
        return hanging;
    }

    const QJsonArray motifs = json.value(u"motifs"_s).toArray();
    if (hanging.type == QcHangingType::ItemFrame) {
        if (!motifs.isEmpty() || json.contains(u"back"_s)) {
            reader.fail(u"only a painting has 'motifs' and a 'back'"_s);
        }
        return hanging;
    }

    if (motifs.isEmpty() || motifs.size() > QcHangingDefinition::maxMotifs) {
        reader.fail(u"a painting needs 1 to %1 'motifs'"_s.arg(QcHangingDefinition::maxMotifs));
        return hanging;
    }
    for (const auto &value : motifs) {
        const QcPaintingMotif motif = parseMotif(value, packNamespace, reader);
        if (std::ranges::any_of(hanging.motifs, [&motif](const QcPaintingMotif &other) { return other.name == motif.name; })) {
            reader.fail(u"motif '%1' twice"_s.arg(motif.name));
        }
        hanging.motifs.append(motif);
    }
    hanging.back = reader.name(u"back"_s, packNamespace);
    if (hanging.back.isEmpty()) {
        reader.fail(u"a painting needs a 'back' texture"_s);
    }
    return hanging;
}

[[nodiscard]] QStringList checkTextures(const QString &item, const QcHangingDefinition &hangs, const QcAssetCatalog &assets)
{
    QStringList problems;
    for (const QcPaintingMotif &motif : hangs.motifs) {
        const QcTextureDefinition *texture = assets.texture(motif.texture);
        if (!texture) {
            problems.append(u"item %1: motif '%2' uses the unknown texture '%3'"_s.arg(item, motif.name, motif.texture));
            continue;
        }
        if (motif.width * motif.height <= 1) {
            continue;
        }
        const QJsonArray tiles = texture->procedural.value(u"tiles"_s).toArray();
        if (tiles.size() != 2 || tiles[0].toInt() != motif.width || tiles[1].toInt() != motif.height) {
            problems.append(u"item %1: motif '%2' is %3 x %4 blocks, so its texture '%5' needs \"tiles\": [%3, %4]"_s.arg(item, motif.name)
                                .arg(motif.width)
                                .arg(motif.height)
                                .arg(motif.texture));
        }
    }
    return problems;
}

} // namespace QcHangingParser

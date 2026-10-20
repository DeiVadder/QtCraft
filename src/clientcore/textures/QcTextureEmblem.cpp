#include "clientcore/textures/QcTextureEmblem.h"

#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace QcTextureEmblem {

namespace {

// Accepts #RRGGBB only: emblems are opaque.
[[nodiscard]] std::optional<QColor> opaqueColor(const QJsonValue &value)
{
    const QString text = value.toString();
    bool ok = false;
    const uint rgb = text.startsWith(u'#') && text.size() == 7 ? QStringView(text).mid(1).toUInt(&ok, 16) : 0;
    if (!ok) {
        return std::nullopt;
    }
    return QColor(int((rgb >> 16) & 0xFF), int((rgb >> 8) & 0xFF), int(rgb & 0xFF));
}

} // namespace

QColor paint(const QcTextureSpec &spec, int x, int y, QColor color)
{
    if (y < spec.bitmap.size() && x < spec.bitmap[y].size()) {
        const auto it = spec.palette.constFind(spec.bitmap[y][x]);
        if (it != spec.palette.constEnd()) {
            return *it;
        }
    }

    // A fine weave: every other thread a little darker.
    return QcTexturePaint::scaled(color, (x + y) % 2 == 0 ? 1.0 : 0.95);
}

QColor paintIcon(const QcTextureSpec &spec, int x, int y, QColor)
{
    if (y < spec.bitmap.size() && x < spec.bitmap[y].size()) {
        const auto it = spec.palette.constFind(spec.bitmap[y][x]);
        if (it != spec.palette.constEnd()) {
            return *it;
        }
    }
    return Qt::transparent;
}

QcResult<QcTextureSpec> read(const QJsonObject &json, QcTextureSpec spec)
{
    const QJsonArray rows = json.value(u"bitmap"_s).toArray();
    if (rows.size() > QcTextureSpec::maxBitmapRows) {
        return qcError(u"'bitmap' has more than %1 rows"_s.arg(QcTextureSpec::maxBitmapRows));
    }
    for (const auto &row : rows) {
        if (!row.isString() || row.toString().size() > QcTexturePaint::size) {
            return qcError(u"'bitmap' rows must be strings of at most %1 characters"_s.arg(QcTexturePaint::size));
        }
        spec.bitmap.append(row.toString());
    }

    const QJsonObject palette = json.value(u"palette"_s).toObject();
    if (palette.size() > QcTextureSpec::maxPaletteEntries) {
        return qcError(u"'palette' has more than %1 colours"_s.arg(QcTextureSpec::maxPaletteEntries));
    }
    for (auto it = palette.constBegin(); it != palette.constEnd(); ++it) {
        const std::optional<QColor> color = opaqueColor(it.value());
        if (it.key().size() != 1 || !color) {
            return qcError(u"'palette' maps single characters to colours '#rrggbb'"_s);
        }
        spec.palette.insert(it.key().front(), *color);
    }
    return spec;
}

} // namespace QcTextureEmblem

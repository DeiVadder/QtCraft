#include "clientcore/texturepacks/QcTexturePackManifest.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtGlobal>

#include <bit>

using namespace Qt::StringLiterals;

namespace {

using Problem = QcTexturePackProblem;

// Reads an optional string field of at most `limit` characters into `target`.
bool readText(const QJsonObject &json, const QString &key, qsizetype limit, QString &target, Problem &problem)
{
    const QJsonValue value = json.value(key);
    if (value.isUndefined()) {
        return true;
    }
    if (!value.isString() || value.toString().size() > limit) {
        problem = {{}, QT_TRANSLATE_NOOP("QcTexturePack", "'%1' in the manifest must be a text of at most %2 characters"),
                   {key, QString::number(limit)}};
        return false;
    }

    target = value.toString().trimmed();
    return true;
}

bool readCredits(const QJsonObject &json, QList<QcTexturePackManifest::Credit> &credits, Problem &problem)
{
    const QJsonValue value = json.value(u"credits"_s);
    if (value.isUndefined()) {
        return true;
    }
    if (!value.isArray() || value.toArray().size() > QcTexturePackManifest::maxCredits) {
        problem = {{}, QT_TRANSLATE_NOOP("QcTexturePack", "'credits' in the manifest must be a list of at most %1 entries"),
                   {QString::number(QcTexturePackManifest::maxCredits)}};
        return false;
    }

    const QJsonArray entries = value.toArray();
    for (const auto &entry : entries) {
        if (!entry.isObject()) {
            problem = {{}, QT_TRANSLATE_NOOP("QcTexturePack", "Every entry of 'credits' must be an object")};
            return false;
        }

        const QJsonObject object = entry.toObject();
        QcTexturePackManifest::Credit credit;
        const qsizetype limit = qsizetype(QcTexturePackManifest::maxNameLength) * 4;
        if (!readText(object, u"author"_s, limit, credit.author, problem) || !readText(object, u"license"_s, limit, credit.license, problem)
            || !readText(object, u"source"_s, limit, credit.source, problem) || !readText(object, u"note"_s, limit, credit.note, problem)) {
            return false;
        }
        credits.append(credit);
    }
    return true;
}

bool readResolution(const QJsonObject &json, int &resolution, Problem &problem)
{
    const QJsonValue value = json.value(u"resolution"_s);
    const double number = value.toDouble(-1.0);
    if (!value.isDouble() || number != double(int(number)) || !QcTexturePackManifest::isValidResolution(int(number))) {
        problem = {{}, QT_TRANSLATE_NOOP("QcTexturePack", "'resolution' must be a power of two from %1 to %2"),
                   {QString::number(QcTexturePackManifest::minResolution), QString::number(QcTexturePackManifest::maxResolution)}};
        return false;
    }

    resolution = int(number);
    return true;
}

bool readFormat(const QJsonObject &json, int &format, Problem &problem)
{
    const QJsonValue value = json.value(u"format"_s);
    if (value.toDouble(-1.0) != QcTexturePackManifest::formatVersion) {
        problem = {{}, QT_TRANSLATE_NOOP("QcTexturePack", "Unsupported manifest format (expected 'format': %1)"),
                   {QString::number(QcTexturePackManifest::formatVersion)}};
        return false;
    }

    format = QcTexturePackManifest::formatVersion;
    return true;
}

bool readPreview(const QJsonObject &json, QString &preview, Problem &problem)
{
    if (!readText(json, u"preview"_s, QcTexturePackManifest::maxNameLength, preview, problem)) {
        return false;
    }
    if (!preview.isEmpty() && (!QcTexturePackManifest::isPlainFileName(preview) || !preview.endsWith(u".png"_s))) {
        problem = {{}, QT_TRANSLATE_NOOP("QcTexturePack", "'preview' must name a PNG file in the pack folder")};
        return false;
    }
    return true;
}

} // namespace

std::expected<QcTexturePackManifest, QcTexturePackProblem> QcTexturePackManifest::parse(const QByteArray &bytes)
{
    Problem problem{QString::fromLatin1(fileName), ""};
    if (bytes.size() > maxBytes) {
        problem.message = QT_TRANSLATE_NOOP("QcTexturePack", "The manifest is larger than %1 KiB");
        problem.arguments = {QString::number(maxBytes / 1024)};
        return std::unexpected(problem);
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        problem.message = QT_TRANSLATE_NOOP("QcTexturePack", "The manifest is not a valid JSON object");
        return std::unexpected(problem);
    }

    const QJsonObject json = document.object();
    QcTexturePackManifest manifest;
    Problem field;
    const bool ok = readFormat(json, manifest.format, field) && readText(json, u"name"_s, maxNameLength, manifest.name, field)
                    && readText(json, u"description"_s, maxTextLength, manifest.description, field)
                    && readText(json, u"license"_s, qsizetype(maxNameLength) * 4, manifest.license, field) && readResolution(json, manifest.resolution, field)
                    && readCredits(json, manifest.credits, field) && readPreview(json, manifest.preview, field);
    if (ok && manifest.name.isEmpty()) {
        field.message = QT_TRANSLATE_NOOP("QcTexturePack", "The manifest needs a 'name'");
    }
    if (!ok || manifest.name.isEmpty()) {
        field.file = problem.file;
        return std::unexpected(field);
    }

    manifest.procedural = json.value(u"procedural"_s).toBool(false);
    return manifest;
}

bool QcTexturePackManifest::isValidResolution(int resolution)
{
    return resolution >= minResolution && resolution <= maxResolution && std::has_single_bit(unsigned(resolution));
}

bool QcTexturePackManifest::isPlainFileName(const QString &name)
{
    return !name.isEmpty() && !name.startsWith(u'.') && !name.contains(u'/') && !name.contains(u'\\') && !name.contains(u':')
           && !name.contains(u"..") && name.size() <= maxNameLength;
}

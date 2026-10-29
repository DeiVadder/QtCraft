// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/texturepacks/QcTexturePackManifest.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtGlobal>

#include <bit>
#include <optional>

using namespace Qt::StringLiterals;

namespace {

using Problem = QcTexturePackProblem;

// Reads the fields of a manifest one by one and keeps the first problem; once there is one, every
// further read gives a default value without looking at the JSON.
class FieldReader
{
public:
    explicit FieldReader(const QJsonObject &json)
        : m_json(json)
    {
    }

    const std::optional<Problem> &problem() const { return m_problem; }

    // An optional string field of at most `limit` characters (empty when missing).
    [[nodiscard]] QString text(const QString &key, qsizetype limit) { return textOf(m_json, key, limit); }
    [[nodiscard]] int format();
    [[nodiscard]] int resolution();
    [[nodiscard]] QList<QcTexturePackManifest::Credit> credits();
    [[nodiscard]] QString preview();

private:
    [[nodiscard]] QString textOf(const QJsonObject &json, const QString &key, qsizetype limit);
    void fail(Problem problem) { m_problem = std::move(problem); }

    const QJsonObject &m_json;
    std::optional<Problem> m_problem;
};

QString FieldReader::textOf(const QJsonObject &json, const QString &key, qsizetype limit)
{
    const QJsonValue value = json.value(key);
    if (m_problem || value.isUndefined()) {
        return {};
    }
    if (!value.isString() || value.toString().size() > limit) {
        fail({{}, QT_TRANSLATE_NOOP("QcTexturePack", "'%1' in the manifest must be a text of at most %2 characters"), {key, QString::number(limit)}});
        return {};
    }
    return value.toString().trimmed();
}

QList<QcTexturePackManifest::Credit> FieldReader::credits()
{
    const QJsonValue value = m_json.value(u"credits"_s);
    if (m_problem || value.isUndefined()) {
        return {};
    }
    if (!value.isArray() || value.toArray().size() > QcTexturePackManifest::maxCredits) {
        fail({{}, QT_TRANSLATE_NOOP("QcTexturePack", "'credits' in the manifest must be a list of at most %1 entries"),
              {QString::number(QcTexturePackManifest::maxCredits)}});
        return {};
    }

    QList<QcTexturePackManifest::Credit> credits;
    const QJsonArray entries = value.toArray();
    for (const auto &entry : entries) {
        if (!entry.isObject()) {
            fail({{}, QT_TRANSLATE_NOOP("QcTexturePack", "Every entry of 'credits' must be an object")});
            return {};
        }

        const QJsonObject object = entry.toObject();
        const qsizetype limit = static_cast<qsizetype>(QcTexturePackManifest::maxNameLength) * 4;
        const QcTexturePackManifest::Credit credit{textOf(object, u"author"_s, limit), textOf(object, u"license"_s, limit),
                                                   textOf(object, u"source"_s, limit), textOf(object, u"note"_s, limit)};
        if (m_problem) {
            return {};
        }
        credits.append(credit);
    }
    return credits;
}

int FieldReader::resolution()
{
    const QJsonValue value = m_json.value(u"resolution"_s);
    const double number = value.toDouble(-1.0);
    if (m_problem) {
        return QcTexturePackManifest::minResolution;
    }
    if (!value.isDouble() || number != static_cast<double>(static_cast<int>(number)) || !QcTexturePackManifest::isValidResolution(static_cast<int>(number))) {
        fail({{}, QT_TRANSLATE_NOOP("QcTexturePack", "'resolution' must be a power of two from %1 to %2"),
              {QString::number(QcTexturePackManifest::minResolution), QString::number(QcTexturePackManifest::maxResolution)}});
        return QcTexturePackManifest::minResolution;
    }
    return static_cast<int>(number);
}

int FieldReader::format()
{
    const QJsonValue value = m_json.value(u"format"_s);
    if (!m_problem && value.toDouble(-1.0) != QcTexturePackManifest::formatVersion) {
        fail({{}, QT_TRANSLATE_NOOP("QcTexturePack", "Unsupported manifest format (expected 'format': %1)"),
              {QString::number(QcTexturePackManifest::formatVersion)}});
    }
    return QcTexturePackManifest::formatVersion;
}

QString FieldReader::preview()
{
    QString preview = text(u"preview"_s, QcTexturePackManifest::maxNameLength);
    if (m_problem) {
        return {};
    }
    if (!preview.isEmpty() && (!QcTexturePackManifest::isPlainFileName(preview) || !preview.endsWith(u".png"_s))) {
        fail({{}, QT_TRANSLATE_NOOP("QcTexturePack", "'preview' must name a PNG file in the pack folder")});
        return {};
    }
    return preview;
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
    FieldReader reader(json);
    manifest.format = reader.format();
    manifest.name = reader.text(u"name"_s, maxNameLength);
    manifest.description = reader.text(u"description"_s, maxTextLength);
    manifest.license = reader.text(u"license"_s, static_cast<qsizetype>(maxNameLength) * 4);
    manifest.resolution = reader.resolution();
    manifest.credits = reader.credits();
    manifest.preview = reader.preview();
    if (reader.problem() || manifest.name.isEmpty()) {
        Problem field = reader.problem().value_or(Problem{{}, QT_TRANSLATE_NOOP("QcTexturePack", "The manifest needs a 'name'")});
        field.file = problem.file;
        return std::unexpected(field);
    }

    manifest.procedural = json.value(u"procedural"_s).toBool(false);
    return manifest;
}

bool QcTexturePackManifest::isValidResolution(int resolution)
{
    return resolution >= minResolution && resolution <= maxResolution && std::has_single_bit(static_cast<unsigned>(resolution));
}

bool QcTexturePackManifest::isPlainFileName(const QString &name)
{
    return !name.isEmpty() && !name.startsWith(u'.') && !name.contains(u'/') && !name.contains(u'\\') && !name.contains(u':')
           && !name.contains(u"..") && name.size() <= maxNameLength;
}

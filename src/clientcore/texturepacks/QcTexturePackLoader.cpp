// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/texturepacks/QcTexturePackLoader.h"

#include "clientcore/materials/QcMaterialMips.h"

#include "clientcore/textures/QcProceduralTexture.h"
#include "common/content/registry/QcContentNames.h"
#include "common/content/registry/QcContentParser.h"

#include <QBuffer>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QtGlobal>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace QcTexturePackLoader {

namespace {

using Problem = QcTexturePackProblem;

constexpr int maxPreviewSide = 512;
constexpr QByteArrayView pngSignature("\x89PNG\r\n\x1a\n", 8);

// A file below textures/ with the texture name it gives.
struct TextureFile
{
    QString key;
    QString path;
    // Relative to the pack folder, for messages.
    QString relative;
    bool image = false;
};

std::expected<QByteArray, Problem> readFile(const QString &path, const QString &relative, qint64 limit)
{
    QFile file(path);
    if (QFileInfo(path).size() > limit) {
        return std::unexpected(Problem{relative, QT_TRANSLATE_NOOP("QcTexturePack", "The file is larger than %1 KiB and was skipped"),
                                       {QString::number(limit / 1024)}});
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(Problem{relative, QT_TRANSLATE_NOOP("QcTexturePack", "The file cannot be read")});
    }

    QByteArray bytes = file.read(limit + 1);
    if (bytes.size() > limit) {
        return std::unexpected(Problem{relative, QT_TRANSLATE_NOOP("QcTexturePack", "The file is larger than %1 KiB and was skipped"),
                                       {QString::number(limit / 1024)}});
    }
    return bytes;
}

// The entries of a folder and whether there were more than the limit.
struct Entries
{
    QFileInfoList list;
    bool truncated = false;
};

// Up to `limit` entries of a folder, sorted by name; the folder is read lazily, so a folder with
// millions of entries costs no more than `limit` of them. Hidden entries are not listed: those with
// the hidden attribute and, on every platform, those whose name starts with a dot (on Windows a
// dot does not hide a file, and macOS leaves ._ files on foreign file systems).
[[nodiscard]] Entries entriesOf(const QString &path, qsizetype limit)
{
    Entries entries;
    QDirIterator iterator(path, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::System);
    while (iterator.hasNext()) {
        if (entries.list.size() >= limit) {
            entries.truncated = true;
            break;
        }
        iterator.next();
        if (iterator.fileName().startsWith(u'.')) {
            continue;
        }
        entries.list.append(iterator.fileInfo());
    }
    std::ranges::sort(entries.list, {}, [](const QFileInfo &entry) { return entry.fileName(); });
    return entries;
}

// Texture files and what was wrong where they were listed.
struct Listing
{
    QList<TextureFile> files;
    QcTexturePackProblems problems;
};

// Plain files of one namespace folder; links and odd names are reported and left out. Only
// lower-case file names and suffixes are accepted.
[[nodiscard]] Listing listNamespace(const QFileInfo &folder, qsizetype limit)
{
    Listing listing;
    const QString space = folder.fileName();
    const Entries entries = entriesOf(folder.filePath(), limit);
    for (const QFileInfo &entry : entries.list) {
        const QString relative = u"textures/%1/%2"_s.arg(space, entry.fileName());
        const QString suffix = entry.suffix();
        const QString name = entry.completeBaseName();
        const bool known = suffix == u"png"_s || suffix == u"json"_s;
        if (entry.isSymLink() || !entry.isFile() || !known || !QcContentNames::isValidPart(name) || name.contains(u'/')) {
            listing.problems.append({relative, QT_TRANSLATE_NOOP("QcTexturePack",
                                                                 "Not a texture file (expected <name>.png or <name>.json with a-z, 0-9, _, - and .)")});
            continue;
        }
        listing.files.append({space + u':' + name, entry.filePath(), relative, suffix == u"png"_s});
    }
    if (entries.truncated) {
        listing.problems.append({u"textures/%1"_s.arg(space), QT_TRANSLATE_NOOP("QcTexturePack", "More than %1 entries; the rest was not read"),
                         {QString::number(limit)}});
    }
    return listing;
}

[[nodiscard]] Listing listTextures(const QString &directory, const Limits &limits)
{
    Listing listing;
    const QFileInfo textures(QDir(directory).filePath(u"textures"_s));
    if (!textures.exists()) {
        return listing;
    }
    if (textures.isSymLink() || !textures.isDir()) {
        listing.problems.append({u"textures"_s, QT_TRANSLATE_NOOP("QcTexturePack", "Expected a folder 'textures' (links are not followed)")});
        return listing;
    }

    const Entries folders = entriesOf(textures.filePath(), limits.maxNamespaces);
    for (const QFileInfo &folder : folders.list) {
        const QString relative = u"textures/%1"_s.arg(folder.fileName());
        if (folder.isSymLink() || !folder.isDir() || !QcContentNames::isValidPart(folder.fileName())) {
            listing.problems.append({relative, QT_TRANSLATE_NOOP("QcTexturePack", "Expected a namespace folder such as textures/qtcraft")});
            continue;
        }
        // Twice the file limits (textures, and maps when they are read): room for reporting wrong files without reading an
        // endless folder.
        const Listing space =
            listNamespace(folder, std::max<qsizetype>(0, (2 * static_cast<qsizetype>(limits.maxFiles + (limits.materialMaps ? limits.maxMapFiles : 0))) - listing.files.size()));
        listing.files.append(space.files);
        listing.problems.append(space.problems);
    }
    if (folders.truncated) {
        listing.problems.append({u"textures"_s, QT_TRANSLATE_NOOP("QcTexturePack", "More than %1 entries; the rest was not read"),
                                 {QString::number(limits.maxNamespaces)}});
    }
    return listing;
}

std::expected<QJsonObject, Problem> readSpec(const TextureFile &file, const Limits &limits)
{
    const std::expected<QByteArray, Problem> bytes = readFile(file.path, file.relative, limits.maxSpecFileBytes);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }

    QJsonParseError error;
    QJsonObject json = QJsonDocument::fromJson(*bytes, &error).object();
    if (error.error != QJsonParseError::NoError) {
        return std::unexpected(Problem{file.relative, QT_TRANSLATE_NOOP("QcTexturePack", "Not a valid JSON object")});
    }

    // The file name gives the texture; a "name" inside the file is not needed.
    const QString space = file.key.section(u':', 0, 0);
    json.insert(u"name"_s, file.key);
    const QcResult<QcTextureDefinition> definition = QcContentParser::parseProceduralTexture(json, space);
    const QcResult<QcTextureSpec> spec = definition ? QcProceduralTexture::spec(definition->procedural) : qcError(definition.error());
    if (!spec) {
        return std::unexpected(Problem{file.relative, QT_TRANSLATE_NOOP("QcTexturePack", "Invalid procedural texture: %1"), {spec.error()}});
    }
    return definition->procedural;
}

// Loads one pack: owns the pack being built and adds its textures within the limits.
class PackLoader
{
public:
    explicit PackLoader(const Limits &limits)
        : m_limits(limits)
    {
    }

    [[nodiscard]] std::expected<QcTexturePack, QcTexturePackProblem> load(const QString &directory, const QString &id, QcTexturePack::Origin origin);

private:
    // Adds one texture file to the pack, or a problem.
    void addTexture(const TextureFile &file);
    // Adds one material map (QcTexturePack::materialMapOf), or a problem.
    void addMap(const TextureFile &file);
    // The colour textures, then (with Limits::materialMaps) the maps, each within its file limit.
    void addFiles(const QList<TextureFile> &files);
    // At most maxProblems problems and one line for the rest.
    void capProblems();

    const Limits &m_limits;
    QcTexturePack m_pack;
    qint64 m_mapBytes = 0;
};

void PackLoader::addTexture(const TextureFile &file)
{
    if (!file.image) {
        const std::expected<QJsonObject, Problem> spec = readSpec(file, m_limits);
        if (spec) {
            m_pack.specs.insert(file.key, *spec);
        } else {
            m_pack.problems.append(spec.error());
        }
        return;
    }

    const std::expected<QByteArray, Problem> bytes = readFile(file.path, file.relative, m_limits.maxImageFileBytes);
    std::expected<Image, Problem> image = bytes ? readImage(*bytes, m_pack.manifest.resolution, m_limits) : std::unexpected(bytes.error());
    if (!image) {
        image.error().file = file.relative;
        m_pack.problems.append(image.error());
        return;
    }

    const qint64 bytesNeeded = static_cast<qint64>(image->image.sizeInBytes()) * image->frames;
    if (m_pack.decodedBytes + bytesNeeded > m_limits.memoryBudget) {
        m_pack.problems.append({file.relative, QT_TRANSLATE_NOOP("QcTexturePack", "Skipped: the pack's images exceed the memory budget of %1 MiB"),
                              {QString::number(m_limits.memoryBudget / (qint64{1024} * 1024))}});
        return;
    }
    m_pack.decodedBytes += bytesNeeded;
    m_pack.images.insert(file.key, image->image);
    if (!image->laterFrames.isEmpty()) {
        m_pack.laterFrames.insert(file.key, image->laterFrames);
    }
}

void PackLoader::addMap(const TextureFile &file)
{
    // Read at the pack's resolution (QImage's smooth scaling would mix the channels where alpha,
    // height or light, is low), then box-filtered down to the stored size.
    Limits unscaled = m_limits;
    unscaled.maxStoredResolution = std::max(m_limits.maxStoredResolution, m_pack.manifest.resolution);
    const std::expected<QByteArray, Problem> bytes = readFile(file.path, file.relative, m_limits.maxImageFileBytes);
    std::expected<Image, Problem> image = bytes ? readImage(*bytes, m_pack.manifest.resolution, unscaled) : std::unexpected(bytes.error());
    if (!image) {
        image.error().file = file.relative;
        m_pack.problems.append(image.error());
        return;
    }

    const std::optional<QcTexturePack::MapKind> found = QcTexturePack::materialMapOf(file.key);
    if (!found) {
        return;
    }
    const QcTexturePack::MapKind kind = *found;
    const int size = std::min(m_pack.manifest.resolution, m_limits.mapResolution);
    const QImage map = kind == QcTexturePack::MapKind::Normal ? QcMaterialMips::normalAt(image->image, size)
                                                              : QcMaterialMips::specularAt(image->image, size);
    if (m_mapBytes + map.sizeInBytes() > m_limits.mapMemoryBudget) {
        m_pack.problems.append({file.relative, QT_TRANSLATE_NOOP("QcTexturePack", "Skipped: the pack's material maps exceed the memory budget of %1 MiB"),
                                {QString::number(m_limits.mapMemoryBudget / (qint64{1024} * 1024))}});
        return;
    }
    m_mapBytes += map.sizeInBytes();
    // The first frame of a strip for every frame of the texture `<name>`.
    (kind == QcTexturePack::MapKind::Normal ? m_pack.normalMaps : m_pack.specularMaps).insert(file.key.chopped(2), map);
}

void PackLoader::addFiles(const QList<TextureFile> &files)
{
    QList<TextureFile> colour;
    QList<TextureFile> maps;
    for (const TextureFile &file : files) {
        (file.image && QcTexturePack::materialMapOf(file.key) ? maps : colour).append(file);
    }
    if (colour.size() > m_limits.maxFiles) {
        m_pack.problems.append({u"textures"_s, QT_TRANSLATE_NOOP("QcTexturePack", "More than %1 texture files; the rest was skipped"),
                                {QString::number(m_limits.maxFiles)}});
        colour.resize(m_limits.maxFiles);
    }
    for (const TextureFile &file : std::as_const(colour)) {
        addTexture(file);
    }

    // Without the realistic mode's maps they are not read at all (the classic mode holds none).
    if (!m_limits.materialMaps) {
        m_pack.unreadMaps = maps.size();
        return;
    }
    if (maps.size() > m_limits.maxMapFiles) {
        m_pack.problems.append({u"textures"_s, QT_TRANSLATE_NOOP("QcTexturePack", "More than %1 material maps; the rest was skipped"),
                                {QString::number(m_limits.maxMapFiles)}});
        maps.resize(m_limits.maxMapFiles);
    }
    for (const TextureFile &file : std::as_const(maps)) {
        addMap(file);
    }
}

void PackLoader::capProblems()
{
    QcTexturePackProblems &problems = m_pack.problems;
    if (problems.size() <= m_limits.maxProblems) {
        return;
    }

    const qsizetype more = problems.size() - m_limits.maxProblems;
    problems.resize(m_limits.maxProblems);
    problems.append({{}, QT_TRANSLATE_NOOP("QcTexturePack", "… and %1 more problems"), {QString::number(more)}});
}

std::expected<QcTexturePack, QcTexturePackProblem> PackLoader::load(const QString &directory, const QString &id, QcTexturePack::Origin origin)
{
    std::expected<QcTexturePackManifest, Problem> manifest = readManifest(directory);
    if (!manifest) {
        return std::unexpected(manifest.error());
    }

    m_pack.id = id;
    m_pack.origin = origin;
    m_pack.directory = directory;
    m_pack.manifest = std::move(*manifest);

    Listing listing = listTextures(directory, m_limits);
    m_pack.problems = std::move(listing.problems);
    addFiles(listing.files);

    std::expected<QImage, Problem> preview = readPreview(directory, m_pack.manifest, m_limits);
    if (preview) {
        m_pack.preview = std::move(*preview);
    } else {
        m_pack.problems.append(preview.error());
    }
    capProblems();
    return std::move(m_pack);
}

} // namespace

std::expected<QImage, QcTexturePackProblem> readPreview(const QString &directory, const QcTexturePackManifest &manifest, const Limits &limits)
{
    if (manifest.preview.isEmpty()) {
        return QImage();
    }

    const QString path = QDir(directory).filePath(manifest.preview);
    const QFileInfo info(path);
    if (info.isSymLink() || !info.isFile()) {
        return std::unexpected(Problem{manifest.preview, QT_TRANSLATE_NOOP("QcTexturePack", "The preview image is missing")});
    }

    const std::expected<QByteArray, Problem> bytes = readFile(path, manifest.preview, limits.maxImageFileBytes);
    if (!bytes || !bytes->startsWith(pngSignature)) {
        return std::unexpected(Problem{manifest.preview, QT_TRANSLATE_NOOP("QcTexturePack", "The preview must be a PNG of at most 512 x 512 pixels")});
    }

    // Only PNG, and the size from the header before anything is decoded.
    QBuffer buffer;
    buffer.setData(*bytes);
    QImageReader reader(&buffer, "png");
    const QSize size = reader.size();
    QImage image;
    if (!size.isValid() || size.width() > maxPreviewSide || size.height() > maxPreviewSide || !reader.read(&image)) {
        return std::unexpected(Problem{manifest.preview, QT_TRANSLATE_NOOP("QcTexturePack", "The preview must be a PNG of at most 512 x 512 pixels")});
    }
    return image.convertToFormat(QImage::Format_RGBA8888);
}

std::expected<QcTexturePackManifest, QcTexturePackProblem> readManifest(const QString &directory)
{
    const QString relative = QString::fromLatin1(QcTexturePackManifest::fileName);
    const QString path = QDir(directory).filePath(relative);
    const QFileInfo info(path);
    if (info.isSymLink() || !info.isFile()) {
        return std::unexpected(Problem{relative, QT_TRANSLATE_NOOP("QcTexturePack", "The pack has no manifest")});
    }

    const std::expected<QByteArray, Problem> bytes = readFile(path, relative, QcTexturePackManifest::maxBytes);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    return QcTexturePackManifest::parse(*bytes);
}

std::expected<QcTexturePack, QcTexturePackProblem> load(const QString &directory, const QString &id, QcTexturePack::Origin origin,
                                                        const Limits &limits)
{
    return PackLoader(limits).load(directory, id, origin);
}

std::expected<Image, QcTexturePackProblem> readImage(const QByteArray &bytes, int resolution, const Limits &limits)
{
    if (!bytes.startsWith(pngSignature)) {
        return std::unexpected(Problem{{}, QT_TRANSLATE_NOOP("QcTexturePack", "Not a PNG image")});
    }

    QBuffer buffer;
    buffer.setData(bytes);
    QImageReader reader(&buffer, "png");
    const QSize size = reader.size();
    if (!size.isValid()) {
        return std::unexpected(Problem{{}, QT_TRANSLATE_NOOP("QcTexturePack", "The image is damaged and cannot be decoded")});
    }

    const int frames = size.height() / std::max(1, size.width());
    if (size.width() != resolution || size.height() % size.width() != 0 || frames < 1 || frames > limits.maxFrames) {
        return std::unexpected(Problem{{},
                                       QT_TRANSLATE_NOOP("QcTexturePack",
                                                         "The image is %1 x %2 pixels; this pack needs %3 x %3 (or a vertical strip of such frames)"),
                                       {QString::number(size.width()), QString::number(size.height()), QString::number(resolution)}});
    }

    QImage image;
    if (!reader.read(&image) || image.size() != size) {
        return std::unexpected(Problem{{}, QT_TRANSLATE_NOOP("QcTexturePack", "The image is damaged and cannot be decoded")});
    }

    const auto frame = [&image, &limits, resolution](int index) {
        QImage square = image.copy(0, index * resolution, resolution, resolution).convertToFormat(QImage::Format_RGBA8888);
        if (resolution > limits.maxStoredResolution) {
            square = square.scaled(limits.maxStoredResolution, limits.maxStoredResolution, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                         .convertToFormat(QImage::Format_RGBA8888);
        }
        return square;
    };
    QList<QImage> laterFrames;
    for (int index = 1; index < frames; ++index) {
        laterFrames.append(frame(index));
    }
    return Image{frame(0), frames, laterFrames};
}

} // namespace QcTexturePackLoader

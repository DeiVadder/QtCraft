#include "clientcore/texturepacks/QcTexturePackLoader.h"

#include "clientcore/textures/QcProceduralTexture.h"
#include "common/content/parser/QcContentParser.h"
#include "common/content/registry/QcContentNames.h"

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

// Up to `limit` entries of a folder, sorted by name; the folder is read lazily, so a folder with
// millions of entries costs no more than `limit` of them. Hidden entries are not listed.
QFileInfoList entriesOf(const QString &path, qsizetype limit, bool &truncated)
{
    QFileInfoList entries;
    QDirIterator iterator(path, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::System);
    while (iterator.hasNext()) {
        if (entries.size() >= limit) {
            truncated = true;
            break;
        }
        iterator.next();
        entries.append(iterator.fileInfo());
    }
    std::ranges::sort(entries, {}, [](const QFileInfo &entry) { return entry.fileName(); });
    return entries;
}

// Plain files of one namespace folder; links and odd names are reported and left out. Only
// lower-case file names and suffixes are accepted.
void listNamespace(const QFileInfo &folder, qsizetype limit, QList<TextureFile> &files, QcTexturePackProblems &problems)
{
    const QString space = folder.fileName();
    bool truncated = false;
    const QFileInfoList entries = entriesOf(folder.filePath(), limit, truncated);
    for (const QFileInfo &entry : entries) {
        const QString relative = u"textures/%1/%2"_s.arg(space, entry.fileName());
        const QString suffix = entry.suffix();
        const QString name = entry.completeBaseName();
        const bool known = suffix == u"png"_s || suffix == u"json"_s;
        if (entry.isSymLink() || !entry.isFile() || !known || !QcContentNames::isValidPart(name) || name.contains(u'/')) {
            problems.append({relative, QT_TRANSLATE_NOOP("QcTexturePack",
                                                         "Not a texture file (expected <name>.png or <name>.json with a-z, 0-9, _, - and .)")});
            continue;
        }
        files.append({space + u':' + name, entry.filePath(), relative, suffix == u"png"_s});
    }
    if (truncated) {
        problems.append({u"textures/%1"_s.arg(space), QT_TRANSLATE_NOOP("QcTexturePack", "More than %1 entries; the rest was not read"),
                         {QString::number(limit)}});
    }
}

QList<TextureFile> listTextures(const QString &directory, const Limits &limits, QcTexturePackProblems &problems)
{
    QList<TextureFile> files;
    const QFileInfo textures(QDir(directory).filePath(u"textures"_s));
    if (!textures.exists()) {
        return files;
    }
    if (textures.isSymLink() || !textures.isDir()) {
        problems.append({u"textures"_s, QT_TRANSLATE_NOOP("QcTexturePack", "Expected a folder 'textures' (links are not followed)")});
        return files;
    }

    bool truncated = false;
    const QFileInfoList folders = entriesOf(textures.filePath(), limits.maxNamespaces, truncated);
    for (const QFileInfo &folder : folders) {
        const QString relative = u"textures/%1"_s.arg(folder.fileName());
        if (folder.isSymLink() || !folder.isDir() || !QcContentNames::isValidPart(folder.fileName())) {
            problems.append({relative, QT_TRANSLATE_NOOP("QcTexturePack", "Expected a namespace folder such as textures/qtcraft")});
            continue;
        }
        // Twice the file limit: room for reporting wrong files without reading an endless folder.
        listNamespace(folder, std::max<qsizetype>(0, 2 * qsizetype(limits.maxFiles) - files.size()), files, problems);
    }
    if (truncated) {
        problems.append({u"textures"_s, QT_TRANSLATE_NOOP("QcTexturePack", "More than %1 entries; the rest was not read"),
                         {QString::number(limits.maxNamespaces)}});
    }
    return files;
}

// At most maxProblems problems and one line for the rest.
void capProblems(QcTexturePackProblems &problems, int maxProblems)
{
    if (problems.size() <= maxProblems) {
        return;
    }

    const qsizetype more = problems.size() - maxProblems;
    problems.resize(maxProblems);
    problems.append({{}, QT_TRANSLATE_NOOP("QcTexturePack", "… and %1 more problems"), {QString::number(more)}});
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

// Adds one texture file to the pack, or a problem.
void addTexture(QcTexturePack &pack, const TextureFile &file, const Limits &limits)
{
    if (!file.image) {
        const std::expected<QJsonObject, Problem> spec = readSpec(file, limits);
        if (spec) {
            pack.specs.insert(file.key, *spec);
        } else {
            pack.problems.append(spec.error());
        }
        return;
    }

    const std::expected<QByteArray, Problem> bytes = readFile(file.path, file.relative, limits.maxImageFileBytes);
    std::expected<Image, Problem> image = bytes ? readImage(*bytes, pack.manifest.resolution, limits) : std::unexpected(bytes.error());
    if (!image) {
        image.error().file = file.relative;
        pack.problems.append(image.error());
        return;
    }

    const qint64 bytesNeeded = qint64(image->image.sizeInBytes());
    if (pack.decodedBytes + bytesNeeded > limits.memoryBudget) {
        pack.problems.append({file.relative, QT_TRANSLATE_NOOP("QcTexturePack", "Skipped: the pack's images exceed the memory budget of %1 MiB"),
                              {QString::number(limits.memoryBudget / (qint64(1024) * 1024))}});
        return;
    }
    if (image->frames > 1) {
        pack.problems.append({file.relative,
                              QT_TRANSLATE_NOOP("QcTexturePack", "Has %1 animation frames; textures are not animated yet, the first frame is used"),
                              {QString::number(image->frames)}});
    }
    pack.decodedBytes += bytesNeeded;
    pack.images.insert(file.key, image->image);
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
    std::expected<QcTexturePackManifest, Problem> manifest = readManifest(directory);
    if (!manifest) {
        return std::unexpected(manifest.error());
    }

    QcTexturePack pack;
    pack.id = id;
    pack.origin = origin;
    pack.directory = directory;
    pack.manifest = std::move(*manifest);

    QList<TextureFile> files = listTextures(directory, limits, pack.problems);
    if (files.size() > limits.maxFiles) {
        pack.problems.append({u"textures"_s, QT_TRANSLATE_NOOP("QcTexturePack", "More than %1 texture files; the rest was skipped"),
                              {QString::number(limits.maxFiles)}});
        files.resize(limits.maxFiles);
    }
    for (const TextureFile &file : std::as_const(files)) {
        addTexture(pack, file, limits);
    }

    std::expected<QImage, Problem> preview = readPreview(directory, pack.manifest, limits);
    if (preview) {
        pack.preview = std::move(*preview);
    } else {
        pack.problems.append(preview.error());
    }
    capProblems(pack.problems, limits.maxProblems);
    return pack;
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

    image = image.copy(0, 0, resolution, resolution).convertToFormat(QImage::Format_RGBA8888);
    if (resolution > limits.maxStoredResolution) {
        image = image.scaled(limits.maxStoredResolution, limits.maxStoredResolution, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                    .convertToFormat(QImage::Format_RGBA8888);
    }
    return Image{image, frames};
}

} // namespace QcTexturePackLoader

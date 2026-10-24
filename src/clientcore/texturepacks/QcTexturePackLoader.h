#pragma once

#include "clientcore/texturepacks/QcTexturePack.h"
#include "common/content/definitions/QcTextureAnimation.h"

#include <QByteArray>
#include <QImage>
#include <QList>

#include <expected>

// Reads a texture pack folder (decision E-297) and checks everything in it at the system boundary
// (decision E-298): the manifest, file names (only `textures/<namespace>/<name>.png|json`, no
// links, no hidden files, so nothing outside the folder is read), the number and size of files,
// PNG signature, size and resolution, damaged images and a budget for decoded image memory. A bad
// file is skipped with a problem and its texture falls back to the next pack; only a missing or
// invalid manifest makes the whole pack unusable.
namespace QcTexturePackLoader {

struct Limits
{
    int maxFiles = 2048;
    // Namespace folders read below textures/.
    int maxNamespaces = 64;
    // Problems kept per pack; the rest is counted in one line.
    int maxProblems = 100;
    qint64 maxImageFileBytes = qint64{4} * 1024 * 1024;
    qint64 maxSpecFileBytes = qint64{64} * 1024;
    // Decoded RGBA bytes of all images of one pack.
    qint64 memoryBudget = qint64{192} * 1024 * 1024;
    // Images above this edge length are stored scaled down (the atlas never uses more, E-300).
    int maxStoredResolution = 256;
    // Vertical strips of animation frames (decision E-878): at most this many frames.
    int maxFrames = QcTextureAnimation::maxFrames;
};

// A checked PNG texture: the first frame at the pack's resolution and how many frames the file had,
// the frames after the first at the same size (a vertical strip animates, decision E-878).
struct Image
{
    QImage image;
    int frames = 1;
    QList<QImage> laterFrames;
};

// The manifest of the folder alone (for listing packs without loading their images).
std::expected<QcTexturePackManifest, QcTexturePackProblem> readManifest(const QString &directory);

std::expected<QcTexturePack, QcTexturePackProblem> load(const QString &directory, const QString &id, QcTexturePack::Origin origin,
                                                        const Limits &limits = {});

// The manifest's preview image (at most 512 x 512 pixels); a null image when the manifest names none.
std::expected<QImage, QcTexturePackProblem> readPreview(const QString &directory, const QcTexturePackManifest &manifest, const Limits &limits = {});

// Checks one PNG file's bytes against the pack's resolution.
std::expected<Image, QcTexturePackProblem> readImage(const QByteArray &bytes, int resolution, const Limits &limits = {});

} // namespace QcTexturePackLoader

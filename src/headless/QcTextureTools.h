#pragma once

#include <QString>
#include <QStringList>

class QcContentRegistry;

// Texture pack tools of qtcraft-headless (stage 28): exporting the content's textures as an image
// texture pack (a starting point for pack authors and how the example pack was made, E-303) and
// measuring the atlas of the shipped packs (size, memory, build time; E-300).
namespace QcTextureTools {

struct ExportRequest
{
    QString directory;
    int resolution = 32;
    // Packs drawn over the content, the first with the highest priority ("builtin:qtcraft_hd_32").
    QStringList packs;
    // Only these texture names ("qtcraft:stone"); all when empty.
    QStringList keys;
    QString name;
    QString modsDirectory;
};

// Writes texturepack.json, textures/<namespace>/<name>.png and preview.png; the exit code.
int exportPack(const QcContentRegistry &content, const ExportRequest &request);

// Builds the atlas without packs and with every built-in pack and prints one line per pack:
// requested and used tile size, atlas size, mip levels, memory and build time (best of three).
int printAtlasStats(const QcContentRegistry &content, const QString &modsDirectory);

} // namespace QcTextureTools

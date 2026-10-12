#pragma once

#include "QcTexturePackProblem.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <expected>

// The manifest `texturepack.json` of a texture pack (decision E-297): what the settings show and
// the resolution every image of the pack must have.
struct QcTexturePackManifest
{
    static constexpr auto fileName = "texturepack.json";
    static constexpr int formatVersion = 1;
    static constexpr int minResolution = 16;
    static constexpr int maxResolution = 512;
    static constexpr qint64 maxBytes = qint64(64) * 1024;
    static constexpr int maxNameLength = 64;
    static constexpr int maxTextLength = 1000;
    static constexpr int maxCredits = 32;

    // Who made (part of) the pack, for packs with material of several authors.
    struct Credit
    {
        QString author;
        QString license;
        QString source;
        QString note;

        friend bool operator==(const Credit &, const Credit &) = default;
    };

    int format = formatVersion;
    QString name;
    QString description;
    // Edge length of one texture in pixels: a power of two from 16 to 512.
    int resolution = minResolution;
    // Licence of the pack as a whole (e.g. "CC0-1.0", "CC-BY-4.0").
    QString license;
    QList<Credit> credits;
    // Renders the procedural textures of the game and of mods (content/*/textures/*.json) at the
    // pack's resolution, below the pack's own files.
    bool procedural = false;
    // Optional preview image in the pack folder: a plain file name ending in .png.
    QString preview;

    // Checks every field; the first problem found is the error.
    static std::expected<QcTexturePackManifest, QcTexturePackProblem> parse(const QByteArray &bytes);
    // A power of two from minResolution to maxResolution.
    static bool isValidResolution(int resolution);
    // A file name inside the pack folder: no path separators, no "..", no hidden file.
    static bool isPlainFileName(const QString &name);

    friend bool operator==(const QcTexturePackManifest &, const QcTexturePackManifest &) = default;
};

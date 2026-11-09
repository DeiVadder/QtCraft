// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/music/QcMusicLibrary.h"

#include "common/content/loader/QcContentPackLoader.h"
#include "common/content/registry/QcJsonFieldReader.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// The *.json files of `directory`, by name; hidden files (macOS's "._name.json") and links are
// left out like the content loader does.
QStringList jsonFilesIn(const QString &directory)
{
    QStringList files;
    if (QFileInfo(directory).isSymLink()) {
        return files;
    }

    const QDir dir(directory);
    const QStringList entries = dir.entryList({u"*.json"_s}, QDir::Files | QDir::NoSymLinks, QDir::Name);
    for (const QString &entry : entries) {
        if (!entry.startsWith(u'.')) {
            files.append(dir.filePath(entry));
        }
    }
    return files;
}

// `name` with `packNamespace` when it has none of its own.
QString qualified(const QString &name, const QString &packNamespace)
{
    return name.contains(u':') ? name : packNamespace + u':' + name;
}

} // namespace

QcMusicLibrary QcMusicLibrary::load(const QStringList &packDirectories)
{
    QcMusicLibrary library;
    for (const QString &directory : packDirectories) {
        library.addPack(directory);
    }
    return library;
}

std::optional<QString> QcMusicLibrary::slotName(const QString &text, const QString &packNamespace)
{
    for (const char *fixed : {menuSlot, daySlot, nightSlot, caveSlot}) {
        if (text == QLatin1StringView(fixed)) {
            return text;
        }
    }
    for (const char *prefix : {biomePrefix, dimensionPrefix}) {
        const QLatin1StringView start(prefix);
        const QString name = text.mid(start.size());
        if (text.startsWith(start) && !name.isEmpty() && !name.startsWith(u':') && !name.endsWith(u':')) {
            return start + qualified(name, packNamespace);
        }
    }
    return std::nullopt;
}

void QcMusicLibrary::addPack(const QString &directory)
{
    const QcResult<QcContentPackInfo> info = QcContentPackLoader::readManifest(directory);
    if (!info) {
        warn(info.error());
        return;
    }

    readBiomes(QDir(directory).filePath(u"biomes"_s), info->id);
    const QStringList tracks = jsonFilesIn(QDir(directory).filePath(u"music"_s));
    for (const QString &file : tracks) {
        readTrack(file, info->id);
    }
}

void QcMusicLibrary::readBiomes(const QString &directory, const QString &packNamespace)
{
    // Only the name and the ambience loop; the server reads and checks the rest.
    const QStringList files = jsonFilesIn(directory);
    for (const QString &file : files) {
        const QcResult<QJsonObject> json = QcContentPackLoader::readJsonObject(file);
        if (!json) {
            continue;
        }

        QcJsonFieldReader reader{*json, file, {}};
        const QString biome = reader.name(u"name"_s, packNamespace);
        const QJsonObject ambience = json->value(u"atmosphere"_s).toObject().value(u"ambience"_s).toObject();
        QcJsonFieldReader nested{ambience, file, {}};
        const QString loop = nested.name(u"loop"_s, packNamespace);
        if (!biome.isEmpty() && !loop.isEmpty()) {
            m_biomeOfLoop.insert(loop, biome);
        }
    }
}

void QcMusicLibrary::readTrack(const QString &file, const QString &packNamespace)
{
    const QcResult<QJsonObject> json = QcContentPackLoader::readJsonObject(file);
    if (!json) {
        warn(json.error());
        return;
    }

    const QFileInfo info(file);
    QcJsonFieldReader reader{*json, u"music track %1"_s.arg(file), {}};
    reader.warnUnknownKeys({u"file", u"title", u"artist", u"licence", u"source", u"attribution", u"slots"});
    QcMusicTrack track;
    track.id = qualified(info.completeBaseName(), packNamespace);
    track.path = info.dir().filePath(reader.string(u"file"_s, info.completeBaseName() + u".ogg"_s));
    track.title = reader.string(u"title"_s, info.completeBaseName());
    track.artist = reader.string(u"artist"_s);
    track.licence = reader.string(u"licence"_s);
    track.source = reader.string(u"source"_s);
    track.attribution = reader.string(u"attribution"_s);
    if (!json->value(u"slots"_s).isArray()) {
        reader.fail(u"'slots' must be an array of places"_s);
    }
    const QJsonArray places = json->value(u"slots"_s).toArray();
    for (const auto &value : places) {
        if (const std::optional<QString> slot = slotName(value.toString(), packNamespace)) {
            track.places.append(*slot);
        } else {
            warn(u"%1: unknown place %2 in 'slots', left out"_s.arg(file, value.toVariant().toString()));
        }
    }

    if (!reader.ok()) {
        warn(reader.error);
    } else if (QFileInfo(track.path).isDir() || !QFileInfo::exists(track.path)) {
        warn(u"%1: the music file %2 is missing"_s.arg(file, track.path));
    } else {
        putTrack(std::move(track));
    }
}

void QcMusicLibrary::putTrack(QcMusicTrack track)
{
    const auto same = std::ranges::find(m_tracks, track.id, &QcMusicTrack::id);
    if (same != m_tracks.end()) {
        *same = std::move(track);
    } else if (m_tracks.size() < maxTracks) {
        m_tracks.append(std::move(track));
    } else {
        warn(u"More than %1 music tracks: %2 left out"_s.arg(maxTracks).arg(track.id));
    }
}

const QcMusicTrack *QcMusicLibrary::track(const QString &id) const
{
    const auto found = std::ranges::find(tracks(), id, &QcMusicTrack::id);
    return found != tracks().end() ? &*found : nullptr;
}

QStringList QcMusicLibrary::tracksFor(const QString &slot) const
{
    QStringList ids;
    for (const QcMusicTrack &track : tracks()) {
        if (track.places.contains(slot)) {
            ids.append(track.id);
        }
    }
    return ids;
}

QStringList QcMusicLibrary::attributions() const
{
    QStringList lines;
    for (const QcMusicTrack &track : tracks()) {
        if (!track.attribution.isEmpty()) {
            lines.append(track.attribution);
        }
    }
    lines.sort(Qt::CaseInsensitive);
    lines.removeDuplicates();
    return lines;
}

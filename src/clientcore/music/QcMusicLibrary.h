// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

/*! \brief One music track of a content pack (\c {music/<name>.json} next to its Ogg Vorbis file,
    decision E-1136). \qcfields */
struct QcMusicTrack
{
    /*! Name with the pack's namespace ("qtcraft:cobalt"); a later pack's track of the same name
        replaces it. */
    QString id;
    /*! The Ogg Vorbis file (on disk or ":/…" in the resources). */
    QString path;
    /*! Title, artist and licence as the credits name them. \qcshared */
    QString title;
    QString artist;
    QString licence;
    /*! Where the track comes from (a web page); may be empty. */
    QString source;
    /*! The credit line its licence asks for, shown in the handbook and under "About". */
    QString attribution;
    /*! The places it plays in (QcMusicLibrary::slotName()). */
    QStringList places;

    friend bool operator==(const QcMusicTrack &, const QcMusicTrack &) = default;
};

/*! \brief The music tracks of the content packs on this computer (decision E-1136): read on the
    client from each pack's \c {music} folder, not sent by the server (like texture packs).

    Also knows which biome an ambience loop belongs to, from the packs' \c {biomes}: the client
    hears the biome's atmosphere, not its name, and the biome slots need the name. */
class QcMusicLibrary
{
public:
    /*! Most tracks of all packs together. */
    static constexpr qsizetype maxTracks = 256;
    /*! Largest track description read, in bytes. */
    static constexpr qint64 maxJsonBytes = 64LL * 1024;
    /*! The fixed places: the menus, and in the overworld day, night and caves. \qcshared */
    static constexpr auto menuSlot = "menu";
    static constexpr auto daySlot = "day";
    static constexpr auto nightSlot = "night";
    static constexpr auto caveSlot = "cave";
    /*! Prefixes of the places of a biome ("biome:nether_wastes") and of a dimension
        ("dimension:nether"). \qcshared */
    static constexpr auto biomePrefix = "biome:";
    static constexpr auto dimensionPrefix = "dimension:";

    /*! Returns the library of the packs in \a packDirectories, in load order. */
    [[nodiscard]] static QcMusicLibrary load(const QStringList &packDirectories);
    /*! Returns \a text as a slot name: a fixed place, or a biome or dimension whose name gets
        \a packNamespace when it has none; none for anything else. */
    [[nodiscard]] static std::optional<QString> slotName(const QString &text, const QString &packNamespace);

    /*! Adds the tracks and biomes of the pack in \a directory; its tracks replace earlier ones of
        the same name. */
    void addPack(const QString &directory);

    /*! Returns every track, in the order they were first added. */
    [[nodiscard]] const QList<QcMusicTrack> &tracks() const { return m_tracks; }
    /*! Returns the track named \a id, or null. */
    [[nodiscard]] const QcMusicTrack *track(const QString &id) const;
    /*! Returns the ids of the tracks that play in \a slot. */
    [[nodiscard]] QStringList tracksFor(const QString &slot) const;
    /*! Returns the biome ("qtcraft:nether_wastes") whose atmosphere plays the ambience loop
        \a loop, or an empty string. */
    [[nodiscard]] QString biomeOfLoop(const QString &loop) const { return m_biomeOfLoop.value(loop); }
    /*! Returns the credit line of every track, sorted, without repeats. */
    [[nodiscard]] QStringList attributions() const;
    /*! Returns what could not be read (files, keys, slots), one line each. */
    [[nodiscard]] const QStringList &warnings() const { return m_warnings; }

private:
    void readBiomes(const QString &directory, const QString &packNamespace);
    void readTrack(const QString &file, const QString &packNamespace);
    void putTrack(QcMusicTrack track);
    void warn(const QString &warning) { m_warnings.append(warning); }

    QList<QcMusicTrack> m_tracks; // NOLINT(qc-member-access): putTrack() is its only writer and keeps the limit
    QHash<QString, QString> m_biomeOfLoop;
    QStringList m_warnings;
};

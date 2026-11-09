// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/settings/QcAudioVolumes.h"

#include <QSettings>

#include <cmath>
#include <optional>

using namespace Qt::StringLiterals;

namespace {

// The one volume of all effects in settings from before the categories (decision E-1151).
QString effectsKey()
{
    return u"audio/effects"_s;
}

// The key of `category` in the settings file.
QString categoryKey(QcSoundCategory category)
{
    return u"audio/"_s + QcSoundCategories::name(category);
}

// The volume stored under `key`, clamped to 0..1; none when it is missing or no number.
std::optional<double> storedVolume(const QSettings &file, const QString &key)
{
    bool ok = false;
    const double value = file.value(key).toDouble(&ok);
    if (!ok || !std::isfinite(value)) {
        return std::nullopt;
    }
    return QcSoundVolumes::clamped(value, 0.0);
}

// The key a category without its own key takes its volume from: settings from before the
// categories (decision E-1151). Music and ambience never changed their keys: none for them, so a
// missing one gives the default.
std::optional<QString> oldKey(QcSoundCategory category)
{
    switch (category) {
    case QcSoundCategory::Music:
    case QcSoundCategory::Ambient:
        return std::nullopt;
    case QcSoundCategory::Weather:
        return u"audio/ambient"_s;
    default:
        return effectsKey();
    }
}

} // namespace

double QcAudioVolumes::defaultVolume(QcSoundCategory category)
{
    switch (category) {
    case QcSoundCategory::Music:
        return 0.5;
    case QcSoundCategory::Ambient:
        return 0.7;
    default:
        return 1.0;
    }
}

QcSoundVolumes QcAudioVolumes::defaultCategories()
{
    QcSoundVolumes volumes;
    for (const QcSoundCategory category : QcSoundCategories::all()) {
        volumes.setVolume(category, defaultVolume(category));
    }
    return volumes;
}

QcAudioVolumes QcAudioVolumes::load(const QSettings &file)
{
    QcAudioVolumes audio;
    audio.master = storedVolume(file, u"audio/master"_s).value_or(defaultMasterVolume);
    for (const QcSoundCategory category : QcSoundCategories::all()) {
        const std::optional<QString> old = oldKey(category);
        const std::optional<double> oldVolume = old ? storedVolume(file, *old) : std::nullopt;
        const std::optional<double> own = storedVolume(file, categoryKey(category));
        audio.categories.setVolume(category, own.value_or(oldVolume.value_or(defaultVolume(category))));
    }

    audio.muted = file.value(u"audio/muted"_s, false).toBool();
    audio.musicSource = QcMusicSources::fromName(file.value(u"audio/musicSource"_s).toString()).value_or(QcMusicSource::Tracks);
    return audio;
}

void QcAudioVolumes::save(QSettings &file) const
{
    file.setValue(u"audio/master"_s, master);
    for (const QcSoundCategory category : QcSoundCategories::all()) {
        file.setValue(categoryKey(category), categories.volume(category));
    }

    // For a game from before the categories (a downgrade): its effects volume follows the
    // blocks. This version reads it only for categories without their own key.
    file.setValue(effectsKey(), categories.volume(QcSoundCategory::Blocks));
    file.setValue(u"audio/muted"_s, muted);
    file.setValue(u"audio/musicSource"_s, QcMusicSources::name(musicSource));
}

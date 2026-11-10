// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/settings/QcAudioSettings.h"

#include "app/debug/QcAppLogging.h"
#include "audio/QcAudioEngine.h"
#include "clientcore/music/QcMusicLibrary.h"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

namespace {

using SoundCategory = QcAudioSettings::SoundCategory;

// Every QML category with the category it stands for, which must have the same value.
constexpr std::array<std::pair<SoundCategory, QcSoundCategory>, qcSoundCategoryCount> categoryPairs{{
    {SoundCategory::Music, QcSoundCategory::Music},
    {SoundCategory::Weather, QcSoundCategory::Weather},
    {SoundCategory::Blocks, QcSoundCategory::Blocks},
    {SoundCategory::Hostile, QcSoundCategory::Hostile},
    {SoundCategory::Friendly, QcSoundCategory::Friendly},
    {SoundCategory::Players, QcSoundCategory::Players},
    {SoundCategory::Ambient, QcSoundCategory::Ambient},
    {SoundCategory::Interface, QcSoundCategory::Interface},
}};
static_assert(std::ranges::all_of(categoryPairs, [](const auto &pair) { return static_cast<int>(pair.first) == static_cast<int>(pair.second); }),
              "AudioSettings.SoundCategory follows QcSoundCategory");

// The sound categories that fall silent while the world pauses (decision E-877).
bool silentWhilePaused(QcSoundCategory category)
{
    return category == QcSoundCategory::Music || category == QcSoundCategory::Weather || category == QcSoundCategory::Ambient;
}

} // namespace

QcAudioSettings::QcAudioSettings(QcAudioEngine *audio, QObject *parent)
    : QObject(parent)
    , m_audio(audio)
    , m_categories(new QcSoundVolumeModel(this, this))
{
}

double QcAudioSettings::checkedVolume(double requested, double current)
{
    const double checked = QcGameSettings::clampVolume(requested, current);
    // The sliders stay inside 0..1; anything else came from outside and is corrected.
    if (checked != requested) {
        qCWarning(lcGame) << "Volume" << requested << "is not in 0..1: using" << checked;
    }
    return checked;
}

void QcAudioSettings::setVolumes(const QcGameSettings::AudioVolumes &volumes)
{
    // The same values again (a slider released where it was) change nothing.
    if (this->volumes() == volumes) {
        return;
    }

    store(volumes);
    apply();
    emit changed();
}

void QcAudioSettings::setMasterVolume(double volume)
{
    QcGameSettings::AudioVolumes next = volumes();
    next.master = checkedVolume(volume, next.master);
    setVolumes(next);
}

double QcAudioSettings::volume(SoundCategory category) const
{
    const std::optional<QcSoundCategory> known = QcSoundCategories::fromIndex(static_cast<int>(category));
    return known ? volumes().categories.volume(*known) : 0.0;
}

void QcAudioSettings::setVolume(SoundCategory category, double volume)
{
    const std::optional<QcSoundCategory> known = QcSoundCategories::fromIndex(static_cast<int>(category));
    if (!known) {
        qCWarning(lcGame) << "Ignoring the volume" << volume << "of the unknown sound category" << static_cast<int>(category);
        return;
    }

    QcGameSettings::AudioVolumes next = volumes();
    next.categories.setVolume(*known, checkedVolume(volume, next.categories.volume(*known)));
    setVolumes(next);
}

void QcAudioSettings::setMuted(bool muted)
{
    QcGameSettings::AudioVolumes next = volumes();
    next.muted = muted;
    setVolumes(next);
}

void QcAudioSettings::setMusicSource(MusicSource source)
{
    if (source != MusicSource::Tracks && source != MusicSource::Generated) {
        qCWarning(lcGame) << "Ignoring unknown music source" << static_cast<int>(source);
        return;
    }

    QcGameSettings::AudioVolumes next = volumes();
    next.musicSource = static_cast<QcMusicSource>(source);
    setVolumes(next);
}

QStringList QcAudioSettings::musicCredits() const
{
    const std::shared_ptr<const QcMusicLibrary> &library = m_audio->music().director().library();
    return library ? library->attributions() : QStringList{};
}

void QcAudioSettings::setMusicLibrary(std::shared_ptr<const QcMusicLibrary> library)
{
    m_audio->music().director().setLibrary(std::move(library));
    emit musicCreditsChanged();
}

void QcAudioSettings::load(const QcGameSettings::AudioVolumes &volumes)
{
    // Applied and reported even when nothing changed: the engine starts from these values.
    store(volumes);
    apply();
    emit changed();
}

void QcAudioSettings::apply() const
{
    QcSoundVolumes heard = volumes().categories;
    if (m_worldPaused) {
        for (const QcSoundCategory category : QcSoundCategories::all()) {
            if (silentWhilePaused(category)) {
                heard.setVolume(category, 0.0);
            }
        }
    }

    m_audio->setVolumes(volumes().master, heard, volumes().muted);
    m_audio->music().director().setSource(volumes().musicSource);
}

void QcAudioSettings::setWorldPaused(bool paused)
{
    if (m_worldPaused != paused) {
        m_worldPaused = paused;
        apply();
    }
}

void QcAudioSettings::retranslate()
{
    m_categories->retranslate();
}

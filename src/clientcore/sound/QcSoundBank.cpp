#include "clientcore/sound/QcSoundBank.h"

#include "clientcore/sound/QcSoundSynthesizer.h"

#include <cmath>

QcSoundBank QcSoundBank::build(const QcContentRegistry &content, int sampleRate)
{
    QcSoundBank bank;
    bank.m_sampleRate = sampleRate;

    for (const QcSoundDefinition &sound : content.assets().sounds()) {
        // Checked before rendering, so an oversized mod sound costs no time either.
        const auto needed = qsizetype(std::ceil(sound.seconds() * sampleRate)) * sound.variants;
        if (bank.m_totalSamples + needed > maxSamples) {
            bank.m_skipped.append(sound.name);
            continue;
        }

        Entry entry;
        entry.definition = sound;
        for (int variant = 0; variant < sound.variants; ++variant) {
            auto buffer = std::make_shared<QcSampleBuffer>(QcSoundSynthesizer::render(sound, variant, sampleRate));
            bank.m_totalSamples += qsizetype(buffer->samples.size());
            entry.variants.append(buffer);
        }

        bank.m_entries.insert(sound.name, entry);
    }
    return bank;
}

const QcSoundBank::Entry *QcSoundBank::find(const QString &name) const
{
    const auto it = m_entries.constFind(name);
    return it == m_entries.constEnd() ? nullptr : &*it;
}

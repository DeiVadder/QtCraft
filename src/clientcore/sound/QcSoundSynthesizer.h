// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/sound/QcSampleBuffer.h"
#include "common/content/registry/QcSoundDefinition.h"

#include <QList>
#include <QString>

/*! \brief Renders sound definitions into PCM.

    Deterministic: the same definition, variant and sample rate always give the same samples.
    Every rendering is normalised to \c {peak}, so it never clips; the layers' gains only weigh
    them against each other and the definition's volume is applied at play time. */
class QcSoundSynthesizer
{
public:
    /*! Sample rate the sounds are rendered at unless asked otherwise. */
    static constexpr int defaultSampleRate = 44100;
    /*! Level every rendering is normalised to. */
    static constexpr float peak = 0.9f;
    /*! Loops are blended over this share of their length (at most maxLoopBlendSeconds) to hide
        the seam. \qcshared */
    static constexpr double loopBlendShare = 0.25;
    static constexpr double maxLoopBlendSeconds = 0.3;
    /*! Non-looping layers fade out over their last few milliseconds so they end without a click. */
    static constexpr double endFadeSeconds = 0.003;

    /*! Returns the seed of \a variant, from the sound's \a name, so that renaming a sound
        changes its noise. */
    static quint32 seedOf(const QString &name, int variant);
    /*! Returns the variant \a variant of \a sound at \a sampleRate. */
    static QcSampleBuffer render(const QcSoundDefinition &sound, int variant, int sampleRate = defaultSampleRate);
    /*! Returns arbitrary \a layers rendered with the noise of \a seed, as a loop when \a loop,
        at \a sampleRate (music is many notes); only the values' finiteness is required. */
    static QcSampleBuffer renderLayers(const QList<QcSoundLayer> &layers, quint32 seed, bool loop,
                                       int sampleRate = defaultSampleRate);
};

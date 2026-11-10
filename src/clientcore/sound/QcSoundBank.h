// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/sound/QcSampleBuffer.h"
#include "clientcore/sound/QcSoundCategory.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/content/registry/QcSoundDefinition.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <memory>

/*! \brief All sounds of the content, rendered once.

    Built on a worker thread and then shared read-only. */
class QcSoundBank
{
public:
    /*! Memory budget for rendered samples (64 MiB of floats); sounds beyond it stay silent. */
    static constexpr qsizetype maxSamples = qsizetype{16} * 1024 * 1024;

    /*! \brief One sound of the content with its rendered variants. */
    struct Entry
    {
        /*! The content's definition of the sound. */
        QcSoundDefinition definition;
        /*! The volume slider it follows (QcSoundCategorizer). */
        QcSoundCategory category = QcSoundCategory::Players;
        /*! The rendered variants; one is picked per play. */
        QList<std::shared_ptr<const QcSampleBuffer>> variants;
    };

    /*! Returns every sound of \a content rendered at \a sampleRate, within maxSamples. */
    static QcSoundBank build(const QcContentRegistry &content, int sampleRate);

    /*! Returns the sound \a name; null for unknown sounds and sounds that did not fit the
        budget. */
    const Entry *find(const QString &name) const;
    /*! Returns the number of sounds rendered. */
    qsizetype size() const { return m_entries.size(); }
    /*! Returns the samples of all rendered sounds together. */
    qsizetype totalSamples() const { return m_totalSamples; }
    /*! Returns the sounds left out because of the memory budget. */
    const QStringList &skipped() const { return m_skipped; }
    /*! Returns the sample rate the sounds were rendered at. */
    int sampleRate() const { return m_sampleRate; }

private:
    QHash<QString, Entry> m_entries;
    qsizetype m_totalSamples = 0;
    QStringList m_skipped;
    int m_sampleRate = 0;
};

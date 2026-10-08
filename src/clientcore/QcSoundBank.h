#pragma once

#include "QcContentRegistry.h"
#include "QcSampleBuffer.h"
#include "QcSoundDefinition.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <memory>

// All sounds of the content, rendered once. Built on a worker thread and then shared read-only.
class QcSoundBank
{
public:
    // Memory budget for rendered samples (64 MiB of floats); sounds beyond it stay silent.
    static constexpr qsizetype maxSamples = qsizetype(16) * 1024 * 1024;

    struct Entry
    {
        QcSoundDefinition definition;
        QList<std::shared_ptr<const QcSampleBuffer>> variants;
    };

    static QcSoundBank build(const QcContentRegistry &content, int sampleRate);

    // Null for unknown sounds and sounds that did not fit the budget.
    const Entry *find(const QString &name) const;
    qsizetype size() const { return m_entries.size(); }
    qsizetype totalSamples() const { return m_totalSamples; }
    // Sounds left out because of the memory budget.
    const QStringList &skipped() const { return m_skipped; }
    int sampleRate() const { return m_sampleRate; }

private:
    QHash<QString, Entry> m_entries;
    qsizetype m_totalSamples = 0;
    QStringList m_skipped;
    int m_sampleRate = 0;
};

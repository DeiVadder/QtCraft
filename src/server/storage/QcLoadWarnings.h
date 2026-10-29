// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QSet>
#include <QString>
#include <QStringList>

#include <utility>

// The warnings of reading a world's saved chunks (blocks, entity and block entity records): the
// readers add to it as they go, so what they found before a damaged part stays. A subject (an
// unknown block, item or creature, keyed with its kind: "block:", "item:", ...) is reported once for
// as long as the object lives: once per world, as QcChunkIo holds one.
class QcLoadWarnings
{
public:
    void add(const QString &text) { m_texts.append(text); }
    void add(const QStringList &texts) { m_texts.append(texts); }

    // Adds `text` unless `subject` was reported before; subjects name their kind ("item:stone"),
    // so a block and an item of the same name are two subjects.
    void addOnce(const QString &subject, const QString &text)
    {
        if (!m_reported.contains(subject)) {
            m_reported.insert(subject);
            m_texts.append(text);
        }
    }

    [[nodiscard]] const QStringList &texts() const { return m_texts; }

    // The warnings added since the last call; the subjects stay reported.
    [[nodiscard]] QStringList takeTexts() { return std::exchange(m_texts, {}); }

private:
    QStringList m_texts;
    QSet<QString> m_reported;
};

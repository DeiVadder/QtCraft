// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

// Texts of the content by language and key (part of QcContentRegistry). Packs add or override
// entries; a lookup falls back to English and finally to the key itself.
class QcTranslations
{
public:
    static constexpr auto fallbackLanguage = "en";

    // Adds or overrides translations for one language.
    void add(const QString &language, const QHash<QString, QString> &entries);

    QStringList languages() const { return m_tables.keys(); }
    qsizetype count(const QString &language) const { return m_tables.value(language).size(); }
    // Entries of all languages together (bounded by QcStreamLimits::maxTranslations, decision E-371).
    qsizetype total() const;
    // Every key with a text in that language (no English fallback), sorted.
    QStringList keys(const QString &language) const;
    // Falls back to English and finally to the key itself.
    QString translate(const QString &key, const QString &language) const;
    bool has(const QString &key, const QString &language) const;
    // Texts per language, then per key (the content sync writes them as they are).
    const QHash<QString, QHash<QString, QString>> &tables() const { return m_tables; }

private:
    QHash<QString, QHash<QString, QString>> m_tables;
};

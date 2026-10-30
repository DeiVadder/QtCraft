// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

/*! \brief Texts of the content by language and key (part of QcContentRegistry).

    Packs add or override entries; a lookup falls back to English and finally to the key itself. */
class QcTranslations
{
public:
    /*! The language a lookup falls back to. */
    static constexpr auto fallbackLanguage = "en";

    /*! Adds the key-text pairs \a entries to \a language, overriding texts of the same key. */
    void add(const QString &language, const QHash<QString, QString> &entries);

    /*! Returns the languages that have texts, in no particular order. */
    QStringList languages() const { return m_tables.keys(); }
    /*! Returns the number of texts in \a language. */
    qsizetype count(const QString &language) const { return m_tables.value(language).size(); }
    /*! Returns the number of entries of all languages together (bounded by
        QcStreamLimits::maxTranslations, decision E-371). */
    qsizetype total() const;
    /*! Returns every key with a text in \a language (no English fallback), sorted. */
    QStringList keys(const QString &language) const;
    /*! Returns the text of \a key in \a language; falls back to English and finally to the key
        itself. */
    QString translate(const QString &key, const QString &language) const;
    /*! Returns whether \a language itself has a text for \a key (no fallback). */
    bool has(const QString &key, const QString &language) const;
    /*! Returns the texts per language, then per key (the content sync writes them as they are). */
    const QHash<QString, QHash<QString, QString>> &tables() const { return m_tables; }

private:
    QHash<QString, QHash<QString, QString>> m_tables;
};

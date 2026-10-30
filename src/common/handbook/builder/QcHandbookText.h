// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"

#include <QLocale>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

/*! \brief The texts of a handbook in one language: translations of content and handbook labels,
    numbers with units, and the rule and key placeholders in descriptions and chapters.

    Collects the problems found while building the handbook. */
class QcHandbookText
{
public:
    QcHandbookText(const QcContentRegistry &content, const QString &language, const QMap<QString, double> &rules,
                   const QMap<QString, QStringList> &keys);

    /*! Language of the English titles every entry carries for the search. */
    static QString english();

    /*! Returns the handbook's language. */
    const QString &language() const { return m_language; }
    /*! Returns the translation of \a key in \a language (English as fallback), empty without
        one. */
    QString lookup(const QString &key, const QString &language) const;
    /*! Returns the translation of \a key in the handbook's language (English as fallback), empty
        without one. */
    QString lookup(const QString &key) const { return lookup(key, m_language); }
    /*! Returns the handbook label \a key ("handbook.<key>"); the key itself shows up when the
        content lacks the translation. */
    QString label(const QString &key) const;
    /*! Returns \a value in the language's number format, rounded to at most two decimals. */
    QString number(double value) const;
    /*! Returns \a value with the unit of seconds. */
    QString seconds(double value) const;
    /*! Returns "yes" or "no" for \a value. */
    QString yesNo(bool value) const;
    /*! Returns \a share (0..1) as a percentage. */
    QString percent(double share) const;
    /*! Returns \a value with the unit of health points. */
    QString healthPoints(double value) const;
    /*! Returns \a value with the unit of food points. */
    QString foodPoints(int value) const;
    /*! Returns the height range from \a minY to \a maxY. */
    QString heightRange(int minY, int maxY) const;
    /*! Returns the name of the machines of the recipe \a category; the category itself without a
        translation. */
    QString machineName(const QString &category) const;
    /*! Returns the name of the smelting recipe \a kind (QcSmeltingRecipe::kind, decision E-770);
        empty: everything else. */
    QString recipeKindName(const QString &kind) const;
    /*! Returns the name of the tool category \a tool; the category itself without a
        translation. */
    QString toolName(const QString &tool) const;
    /*! Returns the note of a link to the source of \a drop: its count or range when not 1 and its
        chance when below 1; empty otherwise. */
    QString dropNote(const QcItemDrop &drop) const;

    /*! Returns the rule value \a name (server rules and content values, see addContentValues()). */
    [[nodiscard]] std::optional<double> rule(const QString &name) const;
    /*! Returns \a text with its placeholders "{name}" filled: key placeholders with key names,
        rules with their values. Unknown names stay as they are and are reported as problems of
        \a context. */
    QString substituteRules(const QString &text, const QString &context);
    /*! Returns the translation of \a key with its placeholders filled (substituteRules(), problems
        reported for \a context); empty without a translation. */
    QString description(const QString &key, const QString &context);

    /*! Records \a problem. */
    void addProblem(const QString &problem) { m_problems.append(problem); }
    /*! Returns the problems recorded so far and forgets them. */
    QStringList takeProblems();

private:
    // Key names in the handbook's language ("handbook.key.<name>", else the name), joined by " / ".
    QString keyText(const QStringList &keys) const;
    // Content values texts may quote: "<block>.toolLevel", ".slots", ".flowDistance", ".light",
    // ".hardness", "<item>.toolLevel", ".food", ".fuelSeconds", ".durability", "<mob>.health".
    void addContentValues();
    void reportUnknownRules(const QStringList &unknown, const QString &context);

    const QcContentRegistry &m_content;
    QString m_language;
    // The server's rule values plus values of the content ("qtcraft:chest.slots", see addContentValues()).
    QMap<QString, double> m_rules;
    // Key placeholders ("key.cameraView") with portable key names.
    QMap<QString, QStringList> m_keys;
    QLocale m_locale;
    QStringList m_problems;
};

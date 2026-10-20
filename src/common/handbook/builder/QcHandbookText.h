#pragma once

#include "common/content/registry/QcContentRegistry.h"

#include <QLocale>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

// The texts of a handbook in one language: translations of content and handbook labels, numbers
// with units, and the rule and key placeholders in descriptions and chapters. Collects the problems
// found while building the handbook.
class QcHandbookText
{
public:
    QcHandbookText(const QcContentRegistry &content, const QString &language, const QMap<QString, double> &rules,
                   const QMap<QString, QStringList> &keys);

    // Language of the English titles every entry carries for the search.
    static QString english();

    const QString &language() const { return m_language; }
    // Translation of a key in `language` (English as fallback), empty without one.
    QString lookup(const QString &key, const QString &language) const;
    QString lookup(const QString &key) const { return lookup(key, m_language); }
    // Handbook label; the key itself shows up when the content lacks the translation.
    QString label(const QString &key) const;
    QString number(double value) const;
    QString seconds(double value) const;
    QString yesNo(bool value) const;
    QString percent(double share) const;
    QString healthPoints(double value) const;
    QString foodPoints(int value) const;
    QString heightRange(int minY, int maxY) const;
    QString machineName(const QString &category) const;
    // Name of a smelting recipe kind (QcSmeltingRecipe::kind, decision E-770); empty: everything else.
    QString recipeKindName(const QString &kind) const;
    QString toolName(const QString &tool) const;
    QString dropNote(const QcItemDrop &drop) const;

    // A rule value (server rules and content values, see addContentValues()).
    [[nodiscard]] std::optional<double> rule(const QString &name) const;
    QString substituteRules(const QString &text, const QString &context);
    // A description with its placeholders filled; empty without a translation.
    QString description(const QString &key, const QString &context);

    void addProblem(const QString &problem) { m_problems.append(problem); }
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

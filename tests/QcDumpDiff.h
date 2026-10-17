#pragma once

#include <QHash>
#include <QStringList>

// Differences between two dumps of comparison runs (decision E-416), readable: a dump is a list of
// sections, each a "== title" line followed by its lines. For each section the lines only the
// expected dump has ("- ") and those only the actual one has ("+ "), under "== title"; sections
// only one side has count with all their lines; a section with the same lines in another order
// is listed in its new order ("~ "). A title that comes again counts as its own section, numbered.
// Empty when the dumps are the same.
namespace QcDumpDiff {

using Sections = QList<std::pair<QString, QStringList>>;

// The sections of a dump in order. A title that comes again gets its count (" #2", " #3", …), so
// every title is unique and repeated sections are compared in the order they come.
inline Sections sectionsOf(const QStringList &dump)
{
    Sections sections;
    QHash<QString, int> seen;
    for (const QString &line : dump) {
        if (line.startsWith(QStringLiteral("== ")) || sections.isEmpty()) {
            const int count = ++seen[line];
            sections.append({count == 1 ? line : line + QStringLiteral(" #%1").arg(count), {}});
            if (line.startsWith(QStringLiteral("== "))) {
                continue;
            }
        }
        sections.back().second.append(line);
    }
    return sections;
}

// The lines of `from` that `other` lacks (counted: a line twice in `from` and once in `other` once).
inline QStringList missingFrom(const QStringList &from, const QStringList &other)
{
    QHash<QString, int> left;
    for (const QString &line : other) {
        ++left[line];
    }
    QStringList missing;
    for (const QString &line : from) {
        if (int &count = left[line]; count > 0) {
            --count;
        } else {
            missing.append(line);
        }
    }
    return missing;
}

// The lines of the section `title` in `sections` (none when it has no such section).
inline QStringList linesOf(const Sections &sections, const QString &title)
{
    for (const auto &[name, lines] : sections) {
        if (name == title) {
            return lines;
        }
    }
    return {};
}

// The section titles of both dumps, those of `before` first.
inline QStringList titlesOf(const Sections &before, const Sections &after)
{
    QStringList titles;
    for (const Sections *sections : {&before, &after}) {
        for (const auto &[name, lines] : *sections) {
            if (!titles.contains(name)) {
                titles.append(name);
            }
        }
    }
    return titles;
}

// The differences of one section, under its title; none when it is the same.
inline QStringList sectionDifferences(const QString &title, const QStringList &old, const QStringList &now)
{
    if (old == now) {
        return {};
    }

    const QStringList removed = missingFrom(old, now);
    const QStringList added = missingFrom(now, old);
    QStringList result;
    if (removed.isEmpty() && added.isEmpty()) {
        // The same lines in another order (the cells of a row are listed in order).
        result.append(title + QStringLiteral(" (same lines, other order)"));
        for (const QString &line : now) {
            result.append(QStringLiteral("~ ") + line);
        }
        return result;
    }
    result.append(title);
    for (const QString &line : removed) {
        result.append(QStringLiteral("- ") + line);
    }
    for (const QString &line : added) {
        result.append(QStringLiteral("+ ") + line);
    }
    return result;
}

inline QStringList differences(const QStringList &expected, const QStringList &actual)
{
    const Sections before = sectionsOf(expected);
    const Sections after = sectionsOf(actual);
    QStringList result;
    const QStringList titles = titlesOf(before, after);
    for (const QString &title : titles) {
        result.append(sectionDifferences(title, linesOf(before, title), linesOf(after, title)));
    }
    return result;
}

} // namespace QcDumpDiff

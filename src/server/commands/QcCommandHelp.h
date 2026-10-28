#pragma once

#include "server/commands/QcCommandRegistry.h"

#include <QList>
#include <QString>

class QcContentRegistry;
struct QcCommandMacroDefinition;

// /help (decisions E-243, E-911): without a word one line per group (QcCommandCategory) with the
// commands the source may run; `/help <group> [page]` those commands with their descriptions,
// pageSize to a page; `/help <command>` its usage, its description and the words its usage cannot
// name (QcCommandSpec::names, the structures of /locate). The macros of the content (decision
// E-248) form the group "other" with their plain texts.
class QcCommandHelp
{
public:
    static constexpr qsizetype pageSize = 8;

    QcCommandHelp(const QcCommandRegistry &registry, const QcContentRegistry &content);

    void run(const QcCommandContext &context) const;
    // Whether the source may run a macro at all.
    static bool mayRun(const QcCommandSource &source, const QcCommandMacroDefinition &macro);

private:
    // The keys of a group's lines: the overview line with %1 its commands, the first line of a page
    // with %1 the page and %2 the number of pages.
    struct Texts
    {
        const char *overview = nullptr;
        const char *page = nullptr;
    };
    static Texts textsOf(QcCommandCategory category);

    // A command /help lists: the registry's (texts are keys) or a macro's (plain texts).
    struct Entry
    {
        QString name;
        QcCommandCategory category = QcCommandCategory::Other;
        const char *description = nullptr;
        QString plainDescription;
    };

    // The commands the source may run, by name.
    [[nodiscard]] QList<Entry> visible(const QcCommandSource &source) const;
    void overview(const QcCommandSource &source) const;
    void page(const QcCommandSource &source, QcCommandCategory category, const QStringList &args) const;
    // `/help <command>`; false when there is no such command.
    bool explain(const QcCommandSource &source, const QString &name) const;

    const QcCommandRegistry &m_registry;
    const QcContentRegistry &m_content;
};

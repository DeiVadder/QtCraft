#pragma once

#include <QList>
#include <QString>

// One page of the handbook: a block, item, creature, biome, chapter ... with its facts, recipes and
// references to other pages. All texts are already translated into the handbook's language.
struct QcHandbookEntry
{
    // A reference to another entry (`target` is its id). `icon` names the item whose icon stands for
    // it, `note` adds a count, chance, height range or the like.
    struct Link
    {
        QString target;
        QString text;
        QString icon;
        QString note;

        bool isEmpty() const { return target.isEmpty() && text.isEmpty(); }
    };

    // One row of the facts table: a value, links, or both.
    struct Fact
    {
        QString label;
        QString value;
        QList<Link> links;
    };

    // A crafting recipe (cells of a grid, row by row; empty links are empty cells) or a machine
    // recipe (one input cell).
    struct Recipe
    {
        enum class Kind { Crafting, Machine };

        Kind kind = Kind::Crafting;
        QString name;
        bool shapeless = false;
        // Needs the 3 x 3 grid of a crafting table.
        bool needsTable = false;
        int width = 1;
        int height = 1;
        QList<Link> cells;
        Link result;
        int resultCount = 1;
        // Machine recipes: seconds per item at machine speed 1 and the machines that process them.
        double seconds = 0.0;
        QList<Link> machines;
        // How and where it is made ("Crafting table, shapeless", "Furnace: 10 s per item, needs fuel").
        QString note;
    };

    // A titled part of the page: text (a small Markdown subset: paragraphs, "- " lists, **bold**),
    // links or recipes.
    struct Section
    {
        QString title;
        QString text;
        QList<Link> links;
        QList<Recipe> recipes;
    };

    // "block:qtcraft:stone", "item:qtcraft:coal", "mob:...", "biome:...", "chapter:...", "tag:...",
    // "ore:...", "feature:...", "world:terrain", "table:fuels", ...
    QString id;
    // Content name the entry describes; empty for overviews.
    QString name;
    QString category;
    // Pack the entry belongs to (the namespace of its name).
    QString pack;
    QString title;
    // Title in English, searched in every language.
    QString englishTitle;
    QString icon;
    // Description from the content's translations; empty when there is none.
    QString text;
    QList<Fact> facts;
    QList<Section> sections;
};

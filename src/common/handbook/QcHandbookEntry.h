// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>

/*! \brief One page of the handbook: a block, item, creature, biome, chapter ... with its facts,
    recipes and references to other pages.

    All texts are already translated into the handbook's language. */
struct QcHandbookEntry
{
    /*! \brief A reference to another entry (\c {target} is its id).

        \c {icon} names the item whose icon stands for it, \c {note} adds a count, chance, height
        range or the like. */
    struct Link
    {
        /*! The id of the entry it leads to. */
        QString target;
        /*! The text shown for the link. */
        QString text;
        /*! The item whose icon stands for it; may be empty. */
        QString icon;
        /*! A count, chance, height range or the like; may be empty. */
        QString note;

        /*! Returns whether it has neither a target nor a text (an empty cell of a recipe). */
        bool isEmpty() const { return target.isEmpty() && text.isEmpty(); }
    };

    /*! \brief One row of the facts table: a value, links, or both. */
    struct Fact
    {
        /*! The row's label. */
        QString label;
        /*! The value; may be empty. */
        QString value;
        /*! The links; may be empty. */
        QList<Link> links;
    };

    /*! \brief A crafting recipe (cells of a grid, row by row; empty links are empty cells) or a
        machine recipe (one input cell). */
    struct Recipe
    {
        /*! The kinds of recipe. */
        enum class Kind {
            /*! A crafting recipe. */
            Crafting,
            /*! A machine recipe. */
            Machine
        };

        /*! The kind of recipe. */
        Kind kind = Kind::Crafting;
        /*! The recipe's content name. */
        QString name;
        /*! Whether the ingredients may lie anywhere in the grid. */
        bool shapeless = false;
        /*! Needs the 3 x 3 grid of a crafting table. */
        bool needsTable = false;
        /*! Width and height of the grid in cells. \qcshared */
        int width = 1;
        int height = 1;
        /*! The cells (see the struct). */
        QList<Link> cells;
        /*! What it makes, and how many. \qcshared */
        Link result;
        int resultCount = 1;
        /*! Machine recipes: seconds per item at machine speed 1 and the machines that process
            them. \qcshared */
        double seconds = 0.0;
        QList<Link> machines;
        /*! How and where it is made ("Crafting table, shapeless", "Furnace: 10 s per item, needs
            fuel"). */
        QString note;
    };

    /*! \brief A titled part of the page: text (a small Markdown subset: paragraphs, "- " lists,
        **bold**), links or recipes. */
    struct Section
    {
        /*! The section's title. */
        QString title;
        /*! Its text (see the struct); may be empty. */
        QString text;
        /*! Its links; may be empty. */
        QList<Link> links;
        /*! Its recipes; may be empty. */
        QList<Recipe> recipes;
    };

    /*! "block:qtcraft:stone", "item:qtcraft:coal", "mob:...", "biome:...", "chapter:...",
        "tag:...", "ore:...", "feature:...", "world:terrain", "table:fuels", ... */
    QString id;
    /*! Content name the entry describes; empty for overviews. */
    QString name;
    /*! The group the entry is listed under ("blocks", "items", "tools", "playing", ...). */
    QString category;
    /*! Pack the entry belongs to (the namespace of its name). */
    QString pack;
    /*! The page's title. */
    QString title;
    /*! Title in English, searched in every language. */
    QString englishTitle;
    /*! The item whose icon stands for the entry; may be empty. */
    QString icon;
    /*! Description from the content's translations; empty when there is none. */
    QString text;
    /*! The rows of the facts table. */
    QList<Fact> facts;
    /*! The parts of the page, in order. */
    QList<Section> sections;
};

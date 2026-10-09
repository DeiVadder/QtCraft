#pragma once

#include <QString>
#include <QStringList>

// A chapter of the handbook about playing (controls, hunger, death ...), from `handbook/*.json`.
// All text comes from translations: the title from `titleKey`, each section from
// "<titleKey>.<section>.title" and "<titleKey>.<section>.text". Server side only.
struct QcHandbookChapter
{
    static constexpr int maxSections = 32;
    static constexpr int maxRelated = 64;

    QString name;
    QString titleKey;
    // Chapters are listed by this number, then by name.
    int order = 100;
    // Item whose icon stands for the chapter; empty for none.
    QString iconItem;
    QStringList sections;
    // Content names (blocks, items, creatures, biomes) the chapter links to.
    QStringList related;
};

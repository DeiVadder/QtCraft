// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

/*! \brief A chapter of the handbook about playing (controls, hunger, death ...), from
    \c {handbook/}\c {*.json}.

    All text comes from translations: the title from \c {titleKey}, each section from
    "<titleKey>.<section>.title" and "<titleKey>.<section>.text". Server side only. */
struct QcHandbookChapter
{
    /*! The most sections of a chapter. */
    static constexpr int maxSections = 32;
    /*! The most related names of a chapter. */
    static constexpr int maxRelated = 64;

    /*! The chapter's content name. */
    QString name;
    /*! The translation key of its title (see the struct). */
    QString titleKey;
    /*! Chapters are listed by this number, then by name. */
    int order = 100;
    /*! Item whose icon stands for the chapter; empty for none. */
    QString iconItem;
    /*! The section names, in order (see the struct). */
    QStringList sections;
    /*! Content names (blocks, items, creatures, biomes) the chapter links to. */
    QStringList related;
};

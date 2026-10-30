// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

class QcContentRegistry;
class QcHandbookIndex;
class QcHandbookLinks;
class QcHandbookRecipes;
class QcHandbookText;

/*! \brief What the handbook page builders share while building one handbook. */
struct QcHandbookContext
{
    /*! The content the handbook describes. */
    const QcContentRegistry &content;
    /*! The texts. Not const: filling placeholders reports problems. */
    QcHandbookText &text;
    /*! Titles, icons and links of the content. */
    const QcHandbookLinks &links;
    /*! Where blocks and items are used and found. */
    const QcHandbookIndex &index;
    /*! The recipes as pages show them. */
    const QcHandbookRecipes &recipes;
};

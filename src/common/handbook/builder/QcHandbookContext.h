#pragma once

class QcContentRegistry;
class QcHandbookIndex;
class QcHandbookLinks;
class QcHandbookRecipes;
class QcHandbookText;

// What the handbook page builders share while building one handbook.
struct QcHandbookContext
{
    const QcContentRegistry &content;
    // Not const: filling placeholders reports problems.
    QcHandbookText &text;
    const QcHandbookLinks &links;
    const QcHandbookIndex &index;
    const QcHandbookRecipes &recipes;
};

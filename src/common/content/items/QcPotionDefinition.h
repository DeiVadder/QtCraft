// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>

#include <array>
#include <optional>

/*! \brief A status effect a potion gives (decision E-1061): its kind
    (QcEffectDefinition::name), how long it lasts and its level (1 = level I). */
struct QcPotionEffect
{
    /*! The longest duration, in seconds. */
    static constexpr double maxSeconds = 3600.0;
    /*! The highest level. */
    static constexpr int maxLevel = 255;

    /*! Qualified name of the status effect (\c {"effect"}, required). */
    QString effect;
    /*! Seconds it lasts (\c {"seconds"}, default 0: an instant effect takes no time). */
    double seconds = 0.0;
    /*! Its level (\c {"level"}, 1 to maxLevel). */
    int level = 1;

    bool operator==(const QcPotionEffect &) const = default;
};

/*! \brief The three items of a potion (decision E-1061): drunk, thrown, and thrown to leave a
    cloud. */
enum class QcPotionForm : quint8 {
    Drink, // Drunk with the use button (\c {"drink"}).
    Splash, // Thrown, it reaches the bodies around where it breaks (\c {"splash"}).
    Lingering, // Thrown like a splash potion at a quarter of the time (\c {"lingering"}).
};
/*! Number of values of QcPotionForm. */
inline constexpr int qcPotionFormCount = 3;
/*! Every form, in the order of the enum. */
inline constexpr std::array<QcPotionForm, qcPotionFormCount> qcPotionForms{QcPotionForm::Drink, QcPotionForm::Splash, QcPotionForm::Lingering};

/*! \brief A kind of potion of a content pack (potions/<name>.json, decision E-1061): what it does
    and how it looks, from which QcPotionGenerator makes its items.

    A longer or stronger version of a potion is a kind of its own that names the potion in
    \c {variantOf}; the brewing recipes reach it by \c {variant}. */
struct QcPotionDefinition
{
    /*! Qualified name (\c {"name"}, required). */
    QString name;
    /*! Translation key of what the potion is called (\c {"translation"}); defaults to
        \c {potion.<namespace>.<name>}. A variant without text takes the one of its potion. */
    QString translationKey;
    /*! The colour of the liquid as 0xRRGGBB (\c {"colour"}); unset: the colour of the first
        effect. */
    std::optional<quint32> colour;
    /*! What it gives (\c {"effects"}); a potion without effects does nothing (a base of
        brewing). */
    QList<QcPotionEffect> effects;
    /*! Which items it has (\c {"forms"}, default all three). */
    QList<QcPotionForm> forms{qcPotionForms.begin(), qcPotionForms.end()};
    /*! The name of its drink item (\c {"item"}); defaults to \c {<name>_potion}. */
    QString item;
    /*! The potion this is a variant of (\c {"variantOf"}); empty for a potion of its own. */
    QString variantOf;
    /*! Which variant it is (\c {"variant"}, like \c {long} or \c {strong}); empty for none. */
    QString variant;

    /*! Returns whether it has items of \a form. */
    bool hasForm(QcPotionForm form) const { return forms.contains(form); }
};

/*! \brief A step of brewing (brewing/<name>.json, decision E-1062): an \c {ingredient} turns the
    potions of the three bottle slots into others, keeping or changing their form.

    Exactly one of three ways: \c {base} becomes \c {result}, each in the form it has; every
    potion becomes its \c {variant}; or every drink becomes the \c {form} of the same potion. */
struct QcBrewingRecipe
{
    /*! Qualified name (\c {"name"}, required). */
    QString name;
    /*! Qualified name of the item used up (\c {"ingredient"}, required). */
    QString ingredient;
    /*! The potion that changes (\c {"base"}); empty for the other two ways. */
    QString base;
    /*! The potion it becomes (\c {"result"}). */
    QString result;
    /*! The variant every potion becomes (\c {"variant"}); empty for the other two ways. */
    QString variant;
    /*! The form every drink becomes (\c {"form"}); QcPotionForm::Drink for the other two ways. */
    QcPotionForm form = QcPotionForm::Drink;
};

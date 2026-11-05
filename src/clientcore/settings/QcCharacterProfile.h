// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/mobs/QcAppearance.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class QSettings;

/*! \brief A look saved under a name: a built-in preset or one the player saved. */
struct QcCharacterPreset
{
    /*! The preset's name. */
    QString name;
    /*! The look. */
    QcAppearance appearance;

    bool operator==(const QcCharacterPreset &) const = default;
};

/*! \brief The player's character profile apart from the look itself: the colours the character
    screen offers, the built-in presets, random looks, and how look and presets are stored in
    settings.ini (decision E-231).

    GUI-free; the server checks every look again (QcProfileValidation). */
namespace QcCharacterProfile {

/*! Presets the player can save at most. */
inline constexpr int maxPresets = 8;
/*! In Unicode code points (QcNameText). */
inline constexpr qsizetype maxPresetNameLength = 32;

/*! Returns the skin colours offered, as 0xRRGGBB; the settings file may hold any other colour. */
const QList<quint32> &skinColors();
/*! Returns the hair colours offered, as 0xRRGGBB. */
const QList<quint32> &hairColors();
/*! Returns the colours offered for shirt and pants, as 0xRRGGBB. */
const QList<quint32> &clothColors();

/*! Returns the presets of the base game; their names are English texts translated in the context
    "QcCharacterProfile". */
const QList<QcCharacterPreset> &builtInPresets();

/*! Returns a random look: an outfit of \a outfits (the default outfit when empty), a hair style of
    \a hairStyles or none, a cape of \a capes or none, and colours from the offered ones. The same
    \a seed gives the same look. */
QcAppearance random(const QStringList &outfits, const QStringList &hairStyles, const QStringList &capes, quint32 seed);

/*! Returns \a name without unpaired surrogates, control and format characters, trimmed, at most
    maxPresetNameLength code points cut between graphemes (QcNameText::clean). */
QString cleanPresetName(const QString &name);

/*! Returns \a appearance as QML sees a look: {outfit, hair, skin, hairColor, shirt, pants, cape}
    with "#rrggbb" colours. */
QVariantMap lookMap(const QcAppearance &appearance);
/*! Returns \a rgb (0xRRGGBB) as "#rrggbb". */
QString hex(quint32 rgb);

/*! Returns the look in \a group ("character") of the settings \a file; names longer than
    QcAppearance::maxNameLength and colours that are not "#rrggbb" fall back to the default
    look's. */
QcAppearance loadAppearance(const QSettings &file, const QString &group);
/*! Stores \a appearance in \a group of \a file. */
void saveAppearance(QSettings &file, const QString &group, const QcAppearance &appearance);
/*! Returns the player's presets ("characterPresets/<n>/...") in the settings \a file; at most
    maxPresets, nameless ones skipped. */
QList<QcCharacterPreset> loadPresets(const QSettings &file);
/*! Stores \a presets in \a file ("characterPresets/<n>/..."). */
void savePresets(QSettings &file, const QList<QcCharacterPreset> &presets);

} // namespace QcCharacterProfile

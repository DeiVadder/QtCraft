// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/sound/QcSoundCategory.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/content/registry/QcSoundDefinition.h"

#include <QHash>
#include <QString>

/*! \brief Finds the QcSoundCategory of each sound of the content (decision E-1150), on the client
    and without help from the server.

    The rules, first match wins, on the name without its namespace: \c {ui.*} is Interface;
    \c {weather.*} and \c {ambient.rain} are Weather; a sound on the music bus is Music, one on the
    ambient bus Ambient, as is \c {ambient.*}; \c {mob.<creature>.*} is Hostile when the creature
    \c {<namespace>:<creature>} is hostile, Friendly when it is not, and for a name that is no
    creature (\c {mob.fish.*}, \c {mob.generic.*}) Hostile only when every creature that names the
    sound is hostile; \c {block.*}, \c {machine.*}, \c {liquid.*}, \c {note.*} and
    \c {entity.explosive.*} are Blocks; another sound a creature names as its own is Hostile or
    Friendly by the same rule; everything else (\c {player.*}, \c {item.*}, \c {water.*},
    \c {entity.*}, unknown names) is Players. */
class QcSoundCategorizer
{
public:
    /*! Learns which creatures of \a content are hostile and which sounds they name. */
    explicit QcSoundCategorizer(const QcContentRegistry &content);

    /*! Returns the category of \a sound. */
    [[nodiscard]] QcSoundCategory category(const QcSoundDefinition &sound) const;

private:
    // Hostile or Friendly for the creature sound `name`, whose path without the namespace `space`
    // is `path` ("mob.<creature>.<what>").
    [[nodiscard]] QcSoundCategory creatureCategory(const QString &space, const QString &path, const QString &name) const;

    // Whether each creature (qualified name) is hostile.
    QHash<QString, bool> m_creatureHostile;
    // Whether every creature that names the sound (qualified name) as its own is hostile.
    QHash<QString, bool> m_soundHostile;
};

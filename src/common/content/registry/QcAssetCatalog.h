// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcTextureDefinition.h"
#include "common/content/mobs/QcAppearanceDefinition.h"
#include "common/content/mobs/QcModelDefinition.h"
#include "common/content/registry/QcNamedList.h"
#include "common/content/registry/QcSoundDefinition.h"

/*! \brief What the client draws and plays (part of QcContentRegistry, synchronised with the
    content): textures, skeleton models, appearances (outfits, hair styles) and sounds, each
    replaced by name. */
class QcAssetCatalog
{
public:
    /*! Adds the texture \a definition, replacing one of the same name. */
    void registerTexture(QcTextureDefinition definition) { m_textures.set(std::move(definition)); }
    /*! Returns the textures in registration order. */
    const QList<QcTextureDefinition> &textures() const { return m_textures.items(); }
    /*! Returns the texture named \a name, or null if there is none. */
    const QcTextureDefinition *texture(const QString &name) const { return m_textures.find(name); }

    /*! Adds the skeleton model \a definition, replacing one of the same name. */
    void registerModel(QcModelDefinition definition) { m_models.set(std::move(definition)); }
    /*! Returns the skeleton models in registration order. */
    const QList<QcModelDefinition> &models() const { return m_models.items(); }
    /*! Returns the skeleton model named \a name, or null if there is none. */
    const QcModelDefinition *model(const QString &name) const { return m_models.find(name); }
    /*! Adds the appearance \a definition, replacing one of the same name. */
    void registerAppearance(QcAppearanceDefinition definition) { m_appearances.set(std::move(definition)); }
    /*! Returns the appearances in registration order. */
    const QList<QcAppearanceDefinition> &appearances() const { return m_appearances.items(); }
    /*! Returns the appearance named \a name, or null if there is none. */
    const QcAppearanceDefinition *appearance(const QString &name) const { return m_appearances.find(name); }

    /*! Adds the sound \a definition, replacing one of the same name. */
    void registerSound(QcSoundDefinition definition) { m_sounds.set(std::move(definition)); }
    /*! Returns the sounds in registration order. */
    const QList<QcSoundDefinition> &sounds() const { return m_sounds.items(); }
    /*! Returns the sound named \a name, or null if there is none. */
    const QcSoundDefinition *sound(const QString &name) const { return m_sounds.find(name); }

private:
    QcNamedList<QcTextureDefinition> m_textures;
    QcNamedList<QcModelDefinition> m_models;
    QcNamedList<QcAppearanceDefinition> m_appearances;
    QcNamedList<QcSoundDefinition> m_sounds;
};

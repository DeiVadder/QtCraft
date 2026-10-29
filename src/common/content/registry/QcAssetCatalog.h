// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcTextureDefinition.h"
#include "common/content/mobs/QcAppearanceDefinition.h"
#include "common/content/mobs/QcModelDefinition.h"
#include "common/content/registry/QcNamedList.h"
#include "common/content/registry/QcSoundDefinition.h"

// What the client draws and plays (part of QcContentRegistry, synchronised with the content):
// textures, skeleton models, appearances (outfits, hair styles) and sounds, each replaced by name.
class QcAssetCatalog
{
public:
    void registerTexture(QcTextureDefinition definition) { m_textures.set(std::move(definition)); }
    const QList<QcTextureDefinition> &textures() const { return m_textures.items(); }
    const QcTextureDefinition *texture(const QString &name) const { return m_textures.find(name); }

    void registerModel(QcModelDefinition definition) { m_models.set(std::move(definition)); }
    const QList<QcModelDefinition> &models() const { return m_models.items(); }
    const QcModelDefinition *model(const QString &name) const { return m_models.find(name); }
    void registerAppearance(QcAppearanceDefinition definition) { m_appearances.set(std::move(definition)); }
    const QList<QcAppearanceDefinition> &appearances() const { return m_appearances.items(); }
    const QcAppearanceDefinition *appearance(const QString &name) const { return m_appearances.find(name); }

    void registerSound(QcSoundDefinition definition) { m_sounds.set(std::move(definition)); }
    const QList<QcSoundDefinition> &sounds() const { return m_sounds.items(); }
    const QcSoundDefinition *sound(const QString &name) const { return m_sounds.find(name); }

private:
    QcNamedList<QcTextureDefinition> m_textures;
    QcNamedList<QcModelDefinition> m_models;
    QcNamedList<QcAppearanceDefinition> m_appearances;
    QcNamedList<QcSoundDefinition> m_sounds;
};

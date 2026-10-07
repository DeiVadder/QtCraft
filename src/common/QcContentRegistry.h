#pragma once

#include "QcBlockDefinition.h"
#include "QcResult.h"
#include "QcTextureDefinition.h"
#include "QcWorldgenDefinitions.h"

#include <QDataStream>
#include <QHash>
#include <QList>
#include <QString>

#include <optional>

// Central catalogue of all content (blocks, textures, translations, world generation). Base game and
// mods register through the same functions; a later registration with the same name replaces the
// earlier one. Runtime block ids follow registration order; air is always id 0.
class QcContentRegistry
{
public:
    static constexpr auto airName = "qtcraft:air";
    static constexpr auto fallbackLanguage = "en";

    QcContentRegistry();

    // Registers a new block or replaces the definition of an existing one (keeping its id).
    QcResult<QcBlockId> registerBlock(QcBlockDefinition definition);
    void registerTexture(QcTextureDefinition definition);
    // Adds or overrides translations for one language.
    void addTranslations(const QString &language, const QHash<QString, QString> &entries);

    qsizetype blockCount() const { return m_blocks.size(); }
    bool isValidBlock(QcBlockId id) const { return qsizetype(id) < m_blocks.size(); }
    // Precondition: isValidBlock(id).
    const QcBlockDefinition &block(QcBlockId id) const;
    std::optional<QcBlockId> blockId(const QString &name) const;
    const QList<QcBlockDefinition> &blocks() const { return m_blocks; }

    const QList<QcTextureDefinition> &textures() const { return m_textures; }
    const QcTextureDefinition *texture(const QString &name) const;

    void registerBiome(QcBiomeDefinition definition);
    void registerFeature(QcFeatureDefinition definition);
    void registerOre(QcOreDefinition definition);
    void setTerrainSettings(QcTerrainSettings settings) { m_terrain = std::move(settings); }
    const QList<QcBiomeDefinition> &biomes() const { return m_biomes; }
    const QList<QcFeatureDefinition> &features() const { return m_features; }
    const QList<QcOreDefinition> &ores() const { return m_ores; }
    const QcTerrainSettings &terrainSettings() const { return m_terrain; }

    QStringList languages() const { return m_translations.keys(); }
    // Falls back to English and finally to the key itself.
    QString translate(const QString &key, const QString &language) const;

    // Wire format used by the content sync message. World-generation data stays on the server.
    void writeTo(QDataStream &stream) const;
    static QcResult<QcContentRegistry> readFrom(QDataStream &stream);

private:
    QList<QcBlockDefinition> m_blocks;
    QHash<QString, QcBlockId> m_blockIds;
    QList<QcTextureDefinition> m_textures;
    QHash<QString, qsizetype> m_textureIndex;
    QHash<QString, QHash<QString, QString>> m_translations;
    QList<QcBiomeDefinition> m_biomes;
    QList<QcFeatureDefinition> m_features;
    QList<QcOreDefinition> m_ores;
    QcTerrainSettings m_terrain;
};

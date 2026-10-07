#pragma once

#include "QcBlockDefinition.h"
#include "QcResult.h"
#include "QcTextureDefinition.h"

#include <QDataStream>
#include <QHash>
#include <QList>
#include <QString>

#include <optional>

// Central catalogue of all content (blocks, textures, translations). Base game and mods register
// through the same functions. Runtime block ids follow registration order; air is always id 0.
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

    QStringList languages() const { return m_translations.keys(); }
    // Falls back to English and finally to the key itself.
    QString translate(const QString &key, const QString &language) const;

    // Wire format used by the content sync message.
    void writeTo(QDataStream &stream) const;
    static QcResult<QcContentRegistry> readFrom(QDataStream &stream);

private:
    QList<QcBlockDefinition> m_blocks;
    QHash<QString, QcBlockId> m_blockIds;
    QList<QcTextureDefinition> m_textures;
    QHash<QString, qsizetype> m_textureIndex;
    QHash<QString, QHash<QString, QString>> m_translations;
};

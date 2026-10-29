// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/preview/QcWorldMapPreview.h"
#include "worldgen/generators/QcWorldOptions.h"

#include <QJsonObject>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>
#include <memory>

class QcContentRegistry;
class QcWorldLibraryController;

// The world generation options of the menu's new world (GameController.worldOptions, decision
// E-917): the options of the base game and the enabled mods in their groups, the presets, and the
// values chosen so far, which "Create and play" stores with the world. The values are part of the
// new world's form: they hold until the form opens again (reset) and count only when the world is
// created.
class QcWorldOptionsController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(WorldOptionsController)
    QML_UNCREATABLE("Provided by GameController.worldOptions")
    // The groups in content order: maps with name, title and options, each option a map with name,
    // label, help, isSwitch, from, to, stepSize, defaultValue (the slider marks it) and unit (Unit).
    Q_PROPERTY(QVariantList groups READ groups NOTIFY optionsChanged FINAL)
    // The presets: maps with name, title and help.
    Q_PROPERTY(QVariantList presets READ presets NOTIFY optionsChanged FINAL)
    // Option name -> value (a number, or true/false for a switch).
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged FINAL)
    // The preset whose values are the chosen ones; "" when none is (the defaults or own values).
    Q_PROPERTY(QString preset READ preset NOTIFY valuesChanged FINAL)
    // How many options differ from their defaults.
    Q_PROPERTY(int changedCount READ changedCount NOTIFY valuesChanged FINAL)
    // The map preview of the world these options make (decision E-921).
    Q_PROPERTY(QcWorldMapPreview *preview READ preview CONSTANT FINAL)

public:
    // How a number reads: as it is, in blocks, in percent of 0..1, or as a factor.
    enum class Unit { Plain, Blocks, Percent, Factor };
    Q_ENUM(Unit)

    // `language` gives the language of the content's texts.
    QcWorldOptionsController(QcWorldLibraryController &library, std::function<QString()> language, QObject *parent = nullptr);
    ~QcWorldOptionsController() override;
    Q_DISABLE_COPY_MOVE(QcWorldOptionsController)

    QVariantList groups() const { return m_groups; }
    QVariantList presets() const { return m_presets; }
    QVariantMap values() const { return chosen().toVariantMap(); }
    QString preset() const;
    int changedCount() const;
    QcWorldMapPreview *preview() const { return m_preview.get(); }

    // Takes the options of the current content (base game and enabled mods), each at its default.
    Q_INVOKABLE void reset();
    // Sets the option `name` (a number within its range, or true/false for a switch); anything else
    // is refused with a warning.
    Q_INVOKABLE void setValue(const QString &name, const QVariant &value);
    // The values of the preset `name`; "" sets every option to its default.
    Q_INVOKABLE void choosePreset(const QString &name);
    // The texts in the current language (after a change of the language).
    void retranslate();
    // Lets go of the content the options point into (a world started: it plays with its own
    // content); the next reset() loads the options again. The values stay.
    void release();

signals:
    void optionsChanged();
    void valuesChanged();

private:
    // The options of the content, loaded when first needed or when the library's content changed.
    void load();
    const QJsonObject &chosen() const { return m_values; }
    void setChosen(const QJsonObject &values);
    QString text(const QString &key) const;
    const std::shared_ptr<const QcContentRegistry> &content() const { return m_content; }
    const QList<QcWorldOptions::Option> &options() const { return m_options; }
    // The content and its options together (both empty: released).
    void setContent(std::shared_ptr<const QcContentRegistry> content);

    QcWorldLibraryController &m_library;
    std::function<QString()> m_language;
    // The content the options point into; kept from reset() until a world starts (release).
    std::shared_ptr<const QcContentRegistry> m_content;
    // The library's contentGeneration() m_content was taken at.
    int m_contentGeneration = -1;
    QList<QcWorldOptions::Option> m_options;
    QVariantList m_groups;
    QVariantList m_presets;
    QJsonObject m_values;
    // Follows the content and the values.
    std::unique_ptr<QcWorldMapPreview> m_preview;
};

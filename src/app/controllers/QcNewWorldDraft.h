// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/controllers/QcWorldLibraryController.h"
#include "common/gameplay/QcGameMode.h"

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

class QcWorldOptionsController;

// The form of the menu's new world (GameController.worldOptions.draft, decision E-956): name, type,
// seed, layers of a flat world, game mode and keeping the inventory, with their defaults whenever
// the form opens (reset) and the random seed of an empty seed field. The world options themselves
// stay in QcWorldOptionsController; setup() hands both to QcGameController::createWorld. The map
// preview follows seed().
class QcNewWorldDraft : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(NewWorldDraft)
    QML_UNCREATABLE("Provided by GameController.worldOptions.draft")
    // The name as typed; createWorld trims it and takes "New World" for an empty one.
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged FINAL)
    Q_PROPERTY(QcWorldLibraryController::WorldType worldType READ worldType WRITE setWorldType NOTIFY worldTypeChanged FINAL)
    // A normal world: it has a seed, world options and a map preview.
    Q_PROPERTY(bool terrain READ terrain NOTIFY worldTypeChanged FINAL)
    // The seed field's text as typed.
    Q_PROPERTY(QString seedText READ seedText WRITE setSeedText NOTIFY seedChanged FINAL)
    // The seed of an empty seed field, drawn when the form opens (the field's placeholder).
    Q_PROPERTY(QString randomSeed READ randomSeed NOTIFY seedChanged FINAL)
    // The seed the world gets: the field's text trimmed, else randomSeed.
    Q_PROPERTY(QString seed READ seed NOTIFY seedChanged FINAL)
    // The layers of a flat world as typed; empty for the default.
    Q_PROPERTY(QString layers READ layers WRITE setLayers NOTIFY layersChanged FINAL)
    Q_PROPERTY(QcGameModes::Mode gameMode READ gameMode WRITE setGameMode NOTIFY gameModeChanged FINAL)
    Q_PROPERTY(bool keepInventory READ keepInventory WRITE setKeepInventory NOTIFY keepInventoryChanged FINAL)

public:
    using WorldType = QcWorldLibraryController::WorldType;
    // Random seeds lie in 0 to maxRandomSeed - 1.
    static constexpr qint64 maxRandomSeed = 2147483647;

    // `options` are the world options of the same form; they outlive the draft.
    explicit QcNewWorldDraft(QcWorldOptionsController &options, QObject *parent = nullptr);

    QString name() const { return m_name; }
    void setName(const QString &name);
    WorldType worldType() const { return m_worldType; }
    // A value that is no world type is refused with a warning.
    void setWorldType(WorldType type);
    bool terrain() const { return worldType() == WorldType::Terrain; }
    QString seedText() const { return m_seedText; }
    void setSeedText(const QString &text);
    QString randomSeed() const { return m_randomSeed; }
    QString seed() const;
    QString layers() const { return m_layers; }
    void setLayers(const QString &layers);
    QcGameMode gameMode() const { return m_gameMode; }
    // A value that is no game mode is refused with a warning.
    void setGameMode(QcGameMode mode);
    bool keepInventory() const { return m_keepInventory; }
    void setKeepInventory(bool keep);
    // What createWorld takes besides name and seed: keepInventory, worldType, layers (a flat
    // world's), gameMode and worldOptions (those of QcWorldOptionsController).
    Q_INVOKABLE QVariantMap setup() const;

    // The type and mode the form starts with (the test hooks QTCRAFT_MENU_WORLD_TYPE and
    // QTCRAFT_MENU_GAME_MODE); values that are none are refused with a warning.
    void setStartWorldType(WorldType type);
    void setStartGameMode(QcGameMode mode);

    // The form opens: every field at its default, a new random seed, the world options at their
    // defaults and the map at its default view.
    Q_INVOKABLE void reset();
    // The die: a new random seed in the field.
    Q_INVOKABLE void rollSeed();
    // A random seed as text, 0 to maxRandomSeed - 1.
    [[nodiscard]] static QString drawSeed();

signals:
    void nameChanged();
    void worldTypeChanged();
    void seedChanged();
    void layersChanged();
    void gameModeChanged();
    void keepInventoryChanged();

private:
    void setRandomSeed(const QString &seed);

    QcWorldOptionsController &m_options;
    QString m_name;
    WorldType m_worldType = WorldType::Terrain;
    WorldType m_startWorldType = WorldType::Terrain;
    QString m_seedText;
    QString m_randomSeed;
    QString m_layers;
    QcGameMode m_gameMode = QcGameMode::Survival;
    QcGameMode m_startGameMode = QcGameMode::Survival;
    bool m_keepInventory = true;
};

#include "app/session/QcWorldStarter.h"

#include "app/controllers/QcSettingsController.h"
#include "app/controllers/QcWorldLibraryController.h"
#include "server/content/QcModLoader.h"
#include "server/core/QcLocalServerHost.h"
#include "server/storage/QcWorldStorage.h"
#include "worldgen/generators/QcWorldGenerators.h"

#include <QDir>
#include <QLoggingCategory>

namespace {

// Messages go to the game's logging category "qtcraft.game", like those of QcGameController.
Q_LOGGING_CATEGORY(lcGame, "qtcraft.game")

} // namespace

QcWorldStarter::QcWorldStarter(QcWorldLibraryController &library, QcSettingsController &settings)
    : m_library(library)
    , m_settings(settings)
{
}

QcResult<std::shared_ptr<const QcContentRegistry>> QcWorldStarter::loadContent()
{
    QStringList warnings;
    QcResult<QcModLoader::Result> content = QcModLoader::load(m_library.modsDirectory(), warnings);
    for (const QString &warning : std::as_const(warnings)) {
        qCWarning(lcGame) << "Content:" << warning;
    }
    if (!content) {
        return std::unexpected(tr("Cannot load game content: %1").arg(content.error()));
    }

    m_library.recordLoadedMods(content->mods);
    m_loadedMods = content->mods;
    std::shared_ptr<const QcContentRegistry> shared = std::move(content->content);
    m_library.useContent(shared);
    m_settings.character()->refreshLists();
    return shared;
}

QString QcWorldStarter::damagedFileMessage(const QcServerPersistence::DamagedFile &damaged)
{
    using Part = QcServerPersistence::DamagedPart;
    switch (damaged.part) {
    case Part::Entities:
        return tr("The file %1 of this world is damaged. Moved out of the world folder, it lets the world open without the creatures, "
                  "items and armour stands saved in it.")
            .arg(damaged.file);
    case Part::BlockEntities:
        return tr("The file %1 of this world is damaged. Moved out of the world folder, it lets the world open without the contents "
                  "of the chests and machines saved in it.")
            .arg(damaged.file);
    case Part::Blocks:
        return tr("The file %1 of this world is damaged. Moved out of the world folder, it lets the world open with the chunks saved "
                  "in it generated anew; their changes are lost.")
            .arg(damaged.file);
    case Part::Journal:
        return tr("The last save of this world broke off and cannot be finished. Removing %1 and every file ending in .next from the world "
                  "folder lets the world open as it was saved the time before.")
            .arg(damaged.file);
    case Part::None:
        break;
    }
    return {};
}

std::expected<std::unique_ptr<QcLocalServerHost>, QcWorldStarter::Refusal>
QcWorldStarter::startServer(const std::shared_ptr<const QcContentRegistry> &content, const QcWorldInfo &world, const QString &directory,
                            std::optional<std::array<int, 2>> spawnColumn)
{
    QcServerConfig config;
    config.worldName = world.name;
    config.seed = world.seed;
    config.generator = world.generator;
    config.generatorOptions = world.generatorOptions;
    config.worldDirectory = directory;
    // One extra ring: sections are meshed only once their neighbours are known.
    config.viewDistance = m_settings.viewDistance() + 1;
    config.spawnColumn = spawnColumn;
    // An unsaved world (test hooks) has no world.json to keep its default game mode.
    if (directory.isEmpty()) {
        config.defaultGameMode = world.defaultGameMode;
    }
    // Options given for an unsaved world (--flat-layers) must fit; a saved world with damaged ones
    // falls back to the defaults with a warning.
    if (const QcStatus checked = QcWorldGenerators::checkOptions(world.generator, world.generatorOptions, *content);
        directory.isEmpty() && !checked) {
        return std::unexpected(Refusal{tr("Cannot start the world: %1").arg(checked.error()), checked.error()});
    }

    QStringList generatorWarnings;
    std::unique_ptr<QcWorldGenerator> generator =
        QcWorldGenerators::create(world.generator, content, config.seed, &generatorWarnings, world.generatorOptions);
    for (const QString &warning : std::as_const(generatorWarnings)) {
        qCWarning(lcGame) << "World generation:" << warning;
    }
    if (!generator) {
        return std::unexpected(Refusal{tr("Cannot load the world: it was made by the unknown world type '%1'").arg(world.generator), std::nullopt});
    }

    auto server = std::make_unique<QcLocalServerHost>(config, content, std::move(generator));
    if (!server->startError().isEmpty()) {
        return std::unexpected(Refusal{tr("Cannot load the world: %1").arg(startError(*server, directory)), std::nullopt});
    }
    if (!directory.isEmpty()) {
        m_library.noteUnplayable(QDir(directory).dirName(), {});
    }
    return server;
}

QString QcWorldStarter::startError(const QcLocalServerHost &server, const QString &directory)
{
    if (server.worldInUse()) {
        return tr("It is already open in another QtCraft window or process. Close it there first.");
    }
    if (server.worldTooOld()) {
        return tr("It was saved by an older development version of QtCraft and cannot be opened.");
    }
    if (const QString damaged = damagedFileMessage(server.damagedFile()); !damaged.isEmpty()) {
        m_library.noteUnplayable(QDir(directory).dirName(), damaged);
        return damaged;
    }
    return server.startError();
}

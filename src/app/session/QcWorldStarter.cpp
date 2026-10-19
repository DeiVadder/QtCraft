#include "app/session/QcWorldStarter.h"

#include "app/controllers/QcSettingsController.h"
#include "app/controllers/QcWorldLibraryController.h"
#include "app/debug/QcAppLogging.h"
#include "common/world/QcWorldInfo.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worlds/QcLocalServerHost.h"
#include "worlds/QcModLoader.h"

#include <QDir>

QcWorldStarter::QcWorldStarter(QcWorldLibraryController &library, QcSettingsController &settings)
    : m_library(library)
    , m_settings(settings)
{
}

QcResult<std::shared_ptr<const QcContentRegistry>> QcWorldStarter::loadContent()
{
    QcModLoader::Loaded content = QcModLoader::load(m_library.modsDirectory());
    for (const QString &warning : QcModLoader::warningsOf(content)) {
        qCWarning(lcGame) << "Content:" << warning;
    }
    if (!content) {
        return std::unexpected(tr("Cannot load game content: %1").arg(content.error().error));
    }

    m_library.recordLoadedMods(content->mods);
    m_loadedMods = content->mods;
    std::shared_ptr<const QcContentRegistry> shared = std::move(content->content);
    m_library.useContent(shared);
    m_settings.character()->refreshLists();
    return shared;
}

QString QcWorldStarter::damagedFileMessage(const QcDamagedFile &damaged)
{
    using Part = QcDamagedPart;
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
    const QcWorldStart start{.world = world, .directory = directory, .viewDistance = m_settings.viewDistance(), .spawnColumn = spawnColumn};
    // Options given for an unsaved world (--flat-layers) must fit; a saved world with damaged ones
    // falls back to the defaults with a warning.
    if (const QcStatus checked = QcWorldGenerators::checkOptions(world.generator, world.generatorOptions, *content);
        directory.isEmpty() && !checked) {
        return std::unexpected(Refusal{tr("Cannot start the world: %1").arg(checked.error()), checked.error()});
    }

    std::unique_ptr<QcWorldGenerator> generator = QcWorldGenerators::create(world.generator, content, world.seed, world.generatorOptions);
    if (!generator) {
        return std::unexpected(Refusal{tr("Cannot load the world: it was made by the unknown world type '%1'").arg(world.generator), std::nullopt});
    }

    for (const QString &warning : generator->warnings()) {
        qCWarning(lcGame) << "World generation:" << warning;
    }
    auto server = std::make_unique<QcLocalServerHost>(start, content, std::move(generator));
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

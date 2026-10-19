#pragma once

#include "common/core/QcResult.h"
#include "common/world/QcDamagedFile.h"
#include "worlds/QcModStatus.h"

#include <QCoreApplication>
#include <QList>
#include <QString>

#include <array>
#include <expected>
#include <memory>
#include <optional>

class QcContentRegistry;
class QcLocalServerHost;
class QcSettingsController;
class QcWorldLibraryController;
struct QcWorldInfo;

// Starts the in-process server of a local world: loads the base game with the enabled mods, builds
// the world generator, starts QcLocalServerHost and turns every refusal into the translated message
// of the error screen (decision E-530). Owns nothing of the running world: the server goes to the
// caller, which keeps it in QcSessionParts.
class QcWorldStarter
{
    Q_DECLARE_TR_FUNCTIONS(QcWorldStarter)

public:
    // Why a world did not start: `status` for the error screen and, when the options of an unsaved
    // world do not suit its type, the reason for QcGameController::worldRefused.
    struct Refusal
    {
        QString status;
        std::optional<QString> refusedOptions;
    };

    QcWorldStarter(QcWorldLibraryController &library, QcSettingsController &settings);

    // Base game plus enabled mods for a new world; the error is the message of the error screen.
    [[nodiscard]] QcResult<std::shared_ptr<const QcContentRegistry>> loadContent();
    // The server of `world` with `content`, saved in `directory` (empty: unsaved), the player
    // spawning in `spawnColumn` when given (test hook).
    [[nodiscard]] std::expected<std::unique_ptr<QcLocalServerHost>, Refusal>
    startServer(const std::shared_ptr<const QcContentRegistry> &content, const QcWorldInfo &world, const QString &directory,
                std::optional<std::array<int, 2>> spawnColumn);
    // The mods that went into the content of the world loaded last.
    const QList<QcModStatus> &loadedMods() const { return m_loadedMods; }

    // The translated message for a world that a damaged per-region file kept from opening (decision
    // E-366); empty when no such file was the reason.
    static QString damagedFileMessage(const QcDamagedFile &damaged);

private:
    // Why `server` did not start; a world kept closed by a damaged file is noted as unplayable.
    QString startError(const QcLocalServerHost &server, const QString &directory);

    QcWorldLibraryController &m_library;
    QcSettingsController &m_settings;
    QList<QcModStatus> m_loadedMods;
};

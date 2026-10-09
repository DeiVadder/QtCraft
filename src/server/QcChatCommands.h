#pragma once

#include "QcBlockDefinition.h"
#include "QcBlockPos.h"
#include "QcResult.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

class QcBlockUpdateQueue;
class QcServerBlocks;
class QcServerCombat;
class QcServerInventory;
struct QcServerClient;
struct QcServerState;

// Chat of the players: plain lines go to everyone, lines starting with '/' are commands (cheat
// commands only when the server allows them).
class QcChatCommands
{
public:
    // Largest volume one /fill command changes.
    static constexpr qint64 maxFillBlocks = 32768;
    // How far /locatebiome searches around the player (blocks).
    static constexpr int biomeSearchRadius = 4096;

    struct Parts
    {
        QcServerBlocks &blocks;
        QcBlockUpdateQueue &updates;
        QcServerInventory &inventory;
        QcServerCombat &combat;
    };
    // Server functions the commands /save and /time use.
    struct Actions
    {
        std::function<QcStatus()> save;
        std::function<void(double seconds)> setWorldTime;
    };

    QcChatCommands(QcServerState &state, const Parts &parts, Actions actions);

    void handle(QcServerClient &client, const QString &text);

private:
    struct Command;
    struct BlockArea;
    using Handler = void (*)(QcChatCommands &chat, const Command &command);
    struct Entry
    {
        QStringView name;
        qsizetype minWords = 1;
        bool cheat = false;
        Handler run = nullptr;
    };

    static const Entry *find(const QString &name);
    void broadcast(const QcServerClient &client, const QString &text);

    void help(const Command &command);
    void gamerule(const Command &command);
    void time(const Command &command) const;
    void summon(const Command &command);
    void spawnItem(const Command &command);
    void setBlock(const Command &command);
    void fill(const Command &command);
    void locateBiome(const Command &command);
    void kill(const Command &command);
    void feed(const Command &command);
    void heal(const Command &command);
    void save(const Command &command) const;
    void give(const Command &command);

    // /setblock and /fill: parses the area and block (replying on errors) and changes the blocks.
    void placeBlocks(const Command &command, bool fill);
    std::optional<BlockArea> parseBlockArea(const Command &command, bool fill) const;
    // Sets one block of the area; false when its chunk is not loaded or no machine fits.
    bool changeBlock(QcBlockPos pos, const BlockArea &area, bool machine);
    // Offset along z of the first column of `name` on the line at `dx` of the square ring `radius`.
    std::optional<int> findBiomeAlong(QcBlockPos feet, int radius, int dx, const QString &name) const;

    QcServerState &m_state;
    Parts m_parts;
    Actions m_actions;
};

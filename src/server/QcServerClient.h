#pragma once

// Server-internal state of one connected client; shared by the QcServer translation units.

#include "QcConnection.h"
#include "QcEntityMotion.h"
#include "QcAppearance.h"
#include "QcInventory.h"
#include "QcServer.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include <memory>
#include <optional>

struct QcServer::Client
{
    enum class State { AwaitingHello, Playing, Closed };

    std::unique_ptr<QcConnection> connection;
    State state = State::AwaitingHello;
    quint32 entityId = 0;
    QString name;
    QcEntityMotion motion;
    int inputBudget = QcServer::maxInputBudget;
    quint32 lastInputSequence = 0;
    bool stateDirty = false;
    QSet<QcChunkPos> sentChunks;
    QcInventory inventory;
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool joined = false;
    std::optional<QcBlockPos> miningPos;
    quint64 miningStartTick = 0;

    double health = QcServer::playerMaxHealth;
    bool dead = false;
    // Highest point of the current fall, for fall damage.
    double fallPeak = 0.0;
    double regenerationTimer = 0.0;
    double hurtTimer = 0.0;
    double attackCooldown = 0.0;
    // Entities this client has been told about.
    QSet<quint32> knownEntities;
    // Known entities whose spawn data changed and must be sent again.
    QSet<quint32> staleEntities;
    // Last stack count and position sent for each visible item entity.
    struct ItemView
    {
        int count = 0;
        QcVec3 position;
    };
    QHash<quint32, ItemView> knownItems;
    // Crafting grid, row-major with three cells per row; a 2 x 2 grid uses the top-left four.
    QList<QcItemStack> craftingGrid = QList<QcItemStack>(9);
    int craftingSize = 2;
    std::optional<QcBlockPos> craftingTable;
    bool sneaking = false;
    bool sprinting = false;
    // Set by actions that swing the arm; reported to other clients in this tick's sync.
    bool swung = false;
    // Set when damage was taken; reported to other clients in this tick's sync.
    bool hurtThisTick = false;
    QcAppearance appearance;
};

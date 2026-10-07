#pragma once

// Server-internal state of one connected client; shared by the QcServer translation units.

#include "QcConnection.h"
#include "QcEntityMotion.h"
#include "QcInventory.h"
#include "QcServer.h"

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
};

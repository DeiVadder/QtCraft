// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/control/QcControlAim.h"
#include "clientcore/session/QcClientSession.h"
#include "fixtures/QcTestUtil.h"
#include "server/core/QcServerConfig.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"
#include "worlds/QcLocalServerHost.h"

#include <QElapsedTimer>
#include <QTest>

#include <cmath>
#include <functional>
#include <memory>

// A local server and client sessions for the tests of the block prediction (decision E-853): joins
// players, waits for conditions while it ticks sessions of TickMode::Manual on the 50 ms grid of real
// time (the server times survival mining in its players' time), sets blocks through the console and
// aims a player at a block face. The helpers return false on failure, so tests check them.
class QcPredictionFixture
{
public:
    static constexpr int timeoutMs = 20'000;

    explicit QcPredictionFixture(std::shared_ptr<const QcContentRegistry> content)
        : m_content(std::move(content))
    {
    }

    ~QcPredictionFixture()
    {
        // The sessions go before the server they talk to.
        m_manual.clear();
        if (m_host) {
            std::ignore = m_host->shutdown();
        }
    }
    Q_DISABLE_COPY_MOVE(QcPredictionFixture)

    QcLocalServerHost &host() { return *m_host; }

    [[nodiscard]] bool startHost(QcGameMode mode, const QString &world)
    {
        QcServerConfig config;
        config.worldName = world;
        config.seed = 7;
        config.viewDistance = 2;
        config.defaultGameMode = mode;
        config.spawnColumn = std::array<int, 2>{8, 8};
        m_host = std::make_unique<QcLocalServerHost>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        return m_host->startError().isEmpty();
    }

    // A session of `name` that plays and stands on the ground; null when it did not get there.
    [[nodiscard]] std::unique_ptr<QcClientSession> join(const QString &name, QcClientSession::TickMode mode)
    {
        auto session = std::make_unique<QcClientSession>(m_host->connectClient(), name);
        if (mode == QcClientSession::TickMode::Manual) {
            m_manual.append(session.get());
        }
        session->start(mode);
        const bool ready = waitFor([&session] {
            return session->state() == QcClientSession::State::Playing && session->playerMotion().onGround && session->world()
                   && session->world()->chunkCount() == 25;
        });
        if (!ready) {
            forget(session.get());
            return nullptr;
        }
        return session;
    }

    // Stops ticking a manual session (before it is destroyed).
    void forget(const QcClientSession *session) { m_manual.removeAll(session); }

    // Processes events, and ticks the manual sessions every 50 ms, until `done` or the time out.
    [[nodiscard]] bool waitFor(const std::function<bool()> &done)
    {
        QElapsedTimer clock;
        clock.start();
        qint64 nextTickMs = 0;
        while (!done()) {
            if (clock.elapsed() > timeoutMs) {
                return false;
            }
            if (clock.elapsed() >= nextTickMs) {
                nextTickMs += 50;
                tickManual();
            }
            QTest::qWait(2);
        }
        return true;
    }

    // Ticks every manual session once.
    void tickManual()
    {
        for (QcClientSession *session : std::as_const(m_manual)) {
            session->tick();
        }
    }

    static QcBlockPos feet(const QcClientSession &session)
    {
        const QcVec3 &position = session.playerMotion().position;
        return {static_cast<int>(std::floor(position.x)), static_cast<int>(std::floor(position.y)), static_cast<int>(std::floor(position.z))};
    }

    // Sets a block through the console and waits until `observer` shows it.
    [[nodiscard]] bool setBlock(const QcClientSession &observer, QcBlockPos pos, const QString &block)
    {
        m_host->runConsoleCommand(QStringLiteral("setblock %1 %2 %3 %4").arg(pos.x).arg(pos.y).arg(pos.z).arg(block));
        const std::optional<QcBlockId> id = m_content->blockId(block);
        return id && waitFor([&] { return observer.world()->blockAt(pos) == id; });
    }

    // A stone floor under the cell and air in it and the two cells above.
    [[nodiscard]] bool clearCellOnStone(const QcClientSession &observer, QcBlockPos cell)
    {
        return setBlock(observer, cell.offset(0, -1, 0), QStringLiteral("qtcraft:stone")) && setBlock(observer, cell, QStringLiteral("qtcraft:air"))
               && setBlock(observer, cell.offset(0, 1, 0), QStringLiteral("qtcraft:air"))
               && setBlock(observer, cell.offset(0, 2, 0), QStringLiteral("qtcraft:air"));
    }

    // A flat stone floor 9 x 9 around the player's feet with air above it up to 3 blocks, so that
    // nothing of the terrain is in the way; waits until `observer` shows it.
    [[nodiscard]] bool flattenAround(const QcClientSession &observer)
    {
        const QcBlockPos feet = QcPredictionFixture::feet(observer);
        const auto fill = [this](QcBlockPos from, QcBlockPos to, const QString &block) {
            m_host->runConsoleCommand(QStringLiteral("fill %1 %2 %3 %4 %5 %6 %7").arg(from.x).arg(from.y).arg(from.z).arg(to.x).arg(to.y).arg(to.z).arg(block));
        };
        fill(feet.offset(-4, -1, -4), feet.offset(4, -1, 4), QStringLiteral("qtcraft:stone"));
        fill(feet.offset(-4, 0, -4), feet.offset(4, 3, 4), QStringLiteral("qtcraft:air"));
        return waitFor([&] {
            return observer.world()->blockAt(feet.offset(4, -1, 4)) == m_content->blockId(QStringLiteral("qtcraft:stone"))
                   && observer.world()->blockAt(feet.offset(4, 3, 4)) == qcAirBlockId && observer.world()->blockAt(feet.offset(-4, 0, -4)) == qcAirBlockId;
        });
    }

    // Turns the player towards the face of the block; true once the hand targets that face.
    [[nodiscard]] static bool aim(QcClientSession &session, QcBlockPos block, QcBlockFace face)
    {
        QcControlAim::lookAt(session, block, face);
        const std::optional<QcRaycastHit> &target = session.hand().target();
        return target && target->block == block && target->face == face;
    }

    // Gives the player of the session a stack (the local player is an operator) and waits until the
    // selected slot holds it.
    [[nodiscard]] bool give(QcClientSession &session, const QString &item, int count)
    {
        session.sendChat(QStringLiteral("/give %1 %2").arg(item).arg(count));
        const std::optional<QcItemId> id = m_content->itemId(item);
        return id && waitFor([&] { return session.inventory().selectedStack().item == *id; });
    }

    // Whether the session waits for no acknowledgement.
    static bool settled(const QcClientSession &session) { return session.worldMirror().blocks().predictedCount() == 0; }

private:
    std::shared_ptr<const QcContentRegistry> m_content;
    std::unique_ptr<QcLocalServerHost> m_host;
    QList<QcClientSession *> m_manual;
};

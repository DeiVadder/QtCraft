#pragma once

#include "clientcore/session/QcChunkExtras.h"
#include "clientcore/session/QcClientWorld.h"
#include "clientcore/weather/QcClientWeather.h"
#include "common/protocol/QcMessages.h"

#include <QList>

#include <memory>
#include <optional>
#include <utility>

// The world as the server sends it (part of QcClientSession, decision E-717): the content, the blocks
// of the loaded chunks (QcClientWorld, there from the content on), what the chunks carry besides
// their blocks (QcChunkExtras), the world time and the weather. It takes the server's world messages
// and tells what they changed; the session reports that through its signals.
class QcWorldMirror
{
public:
    // What a world message changed.
    struct Change
    {
        // Empty while the message was valid; else why the session closes the connection.
        QString error;
        bool contentReady = false;
        std::optional<QcChunkPos> loaded;
        std::optional<QcChunkPos> unloaded;
        // The blocks set, each with the block before.
        QList<std::pair<QcBlockPos, QcBlockId>> blocks;
    };

    // The change a world message made, none for a message of another group; `eye` hears the
    // thunder of a lightning strike, `now` is the client's tick count.
    [[nodiscard]] std::optional<Change> handle(const QcMessage &message, const QcVec3 &eye, quint64 now);
    // One client tick: explosions fade, the world time runs on while the time of day runs.
    void tick(quint64 now);

    // Null until the content arrived.
    const QcClientWorld *world() const { return m_world.get(); }
    const std::shared_ptr<const QcContentRegistry> &content() const { return m_content; }
    // Seconds of world time, advanced locally between server updates while the time of day runs, by
    // timeSpeed() per tick (the server's tick rate or sprint, WorldTime::speed).
    double worldTime() const { return m_worldTime.time; }
    double timeSpeed() const { return m_worldTime.speed; }
    // The weather as shown: strengths, climate, lightning flashes (decision E-395).
    QcClientWeather &weather() { return m_weather; }
    const QcClientWeather &weather() const { return m_weather; }
    // Labels (E-222), spinning spawner creatures (E-520) and explosions (E-604) of the loaded chunks.
    const QcChunkExtras &extras() const { return m_extras; }

private:
    // Content, chunks and blocks; none for other messages.
    [[nodiscard]] std::optional<Change> handleChunkMessage(const QcMessage &message);
    // Labels, spawners, explosions, time and weather; false for other messages.
    bool handleSkyMessage(const QcMessage &message, const QcVec3 &eye, quint64 now);
    [[nodiscard]] Change onContentSync(const QcMsg::ContentSync &sync);
    [[nodiscard]] Change onChunkData(const QcMsg::ChunkData &chunkData);
    [[nodiscard]] Change onBlockUpdate(QcMsg::BlockUpdate update);

    std::shared_ptr<const QcContentRegistry> m_content;
    std::unique_ptr<QcClientWorld> m_world;
    QcChunkExtras m_extras;
    // The latest world time from the server.
    QcMsg::WorldTime m_worldTime{0.0, 600.0, true, 1.0};
    QcClientWeather m_weather;
};

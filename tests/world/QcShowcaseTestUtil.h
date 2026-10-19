#pragma once

#include "common/content/loader/QcBaseContent.h"
#include "server/core/QcServer.h"
#include "worldgen/generators/QcWorldGenerators.h"

#include <QCoreApplication>

#include <memory>

// Helpers of the showcase world tests: the base content, a showcase server and handing queued
// messages on.
namespace QcShowcaseTestUtil {

inline std::shared_ptr<const QcContentRegistry> loadContent()
{
    QStringList warnings;
    return *QcBaseContent::load(warnings);
}

// Delivers the messages queued between a server and an in-process client.
inline void pump()
{
    for (int i = 0; i < 5; ++i) {
        QCoreApplication::processEvents();
    }
}

// A server for a world of `generator` in `directory` that keeps every loaded chunk: the setup is
// looked at as a whole, without a player near it.
inline std::unique_ptr<QcServer> makeServer(const std::shared_ptr<const QcContentRegistry> &content, const QString &directory,
                                            const QString &generator, qint64 seed)
{
    QcServerConfig config;
    config.worldDirectory = directory;
    config.generator = generator;
    config.seed = seed;
    config.viewDistance = 1;
    config.autosaveSeconds = 0;
    config.keepChunksLoaded = true;
    return std::make_unique<QcServer>(config, content, QcWorldGenerators::create(generator, content, seed));
}

} // namespace QcShowcaseTestUtil

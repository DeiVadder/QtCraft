#pragma once

#include "server/core/QcServer.h"
#include "worldgen/generators/QcWorldGenerators.h"

#include <memory>

// Helpers of the showcase world tests: a server for the showcase world (the content and pump()
// come from QcTestUtil).
namespace QcShowcaseTestUtil {

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
    return std::make_unique<QcServer>(config, content, QcWorldGenerators::create(generator, content, seed, {}, QcGeneratorVersion::current));
}

} // namespace QcShowcaseTestUtil

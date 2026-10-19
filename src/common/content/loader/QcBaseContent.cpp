#include "common/content/loader/QcBaseContent.h"

namespace QcBaseContent {

QcResult<QcLoadedPack> loadInto(QcContentRegistry &registry)
{
    QcResult<QcLoadedPack> loaded = QcContentPackLoader::load(QString::fromLatin1(resourceDirectory), registry);
    if (loaded) {
        registry.addPack({loaded->info.id, loaded->info.name, loaded->info.version});
    }
    return loaded;
}

QcResult<Loaded> load()
{
    auto registry = std::make_shared<QcContentRegistry>();
    const QcResult<QcLoadedPack> loaded = loadInto(*registry);
    if (!loaded) {
        return qcError(loaded.error());
    }
    QStringList warnings = loaded->warnings + registry->finish();
    return Loaded{std::move(registry), std::move(warnings)};
}

} // namespace QcBaseContent

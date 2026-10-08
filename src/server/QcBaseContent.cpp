#include "QcBaseContent.h"

namespace QcBaseContent {

QcResult<QcContentPackInfo> loadInto(QcContentRegistry &registry, QStringList &warnings)
{
    QcResult<QcContentPackInfo> info = QcContentPackLoader::load(QString::fromLatin1(resourceDirectory), registry, warnings);
    if (info) {
        registry.addPack({info->id, info->name, info->version});
    }
    return info;
}

QcResult<std::shared_ptr<QcContentRegistry>> load(QStringList &warnings)
{
    auto registry = std::make_shared<QcContentRegistry>();
    const QcResult<QcContentPackInfo> info = loadInto(*registry, warnings);
    if (!info) {
        return qcError(info.error());
    }
    registry->createBlockItems();
    return registry;
}

} // namespace QcBaseContent

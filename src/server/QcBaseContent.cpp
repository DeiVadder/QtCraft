#include "QcBaseContent.h"

#include "QcContentPackLoader.h"

namespace QcBaseContent {

QcResult<std::shared_ptr<QcContentRegistry>> load(QStringList &warnings)
{
    auto registry = std::make_shared<QcContentRegistry>();
    const QcResult<QcContentPackInfo> info =
        QcContentPackLoader::load(QString::fromLatin1(resourceDirectory), *registry, warnings);
    if (!info) {
        return qcError(info.error());
    }
    registry->createBlockItems();
    return registry;
}

} // namespace QcBaseContent

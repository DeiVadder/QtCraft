#include "QcDataRoot.h"

#include <QStandardPaths>

namespace QcDataRoot {

QString path()
{
    const QString overridden = qEnvironmentVariable("QTCRAFT_DATA_DIR");
    return overridden.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : overridden;
}

} // namespace QcDataRoot

// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/core/QcDataRoot.h"

#include <QStandardPaths>

namespace QcDataRoot {

QString path()
{
    const QString overridden = qEnvironmentVariable("QTCRAFT_DATA_DIR");
    return overridden.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : overridden;
}

} // namespace QcDataRoot

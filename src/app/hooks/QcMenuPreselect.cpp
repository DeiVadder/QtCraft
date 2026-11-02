// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/hooks/QcMenuPreselect.h"

#include "app/controllers/QcWorldLibraryController.h"
#include "app/debug/QcAppLogging.h"
#include "common/gameplay/QcGameMode.h"
#include "worldgen/generators/QcWorldGenerators.h"

#include <QObject>
#include <QVariant>

using namespace Qt::StringLiterals;

namespace {

// `root` itself when it has the object name `name`, else its child of that name; null when none.
QObject *namedObject(QObject &root, const QString &name)
{
    return root.objectName() == name ? &root : root.findChild<QObject *>(name);
}

// Chooses `value` in the dropdown `choiceName` below `root` and makes it the page's start choice
// (`startProperty`); a value the dropdown does not offer is refused with a warning.
void preselect(QObject &root, const QString &choiceName, const QString &startProperty, int value)
{
    QObject *choice = namedObject(root, choiceName);
    int index = -1;
    if (!choice || !QMetaObject::invokeMethod(choice, "indexOfValue", Q_RETURN_ARG(int, index), Q_ARG(QVariant, value)) || index < 0) {
        qCWarning(lcStartup) << "Test hook: cannot preselect" << value << "in" << choiceName;
        return;
    }
    choice->setProperty("currentIndex", index);
    if (QObject *page = namedObject(root, u"newWorldPage"_s)) {
        page->setProperty(qPrintable(startProperty), index);
    }
}

} // namespace

namespace QcMenuPreselect {

void newWorld(QObject &root, const QString &worldType, const QString &gameMode)
{
    if (!worldType.isEmpty()) {
        const auto type = QcWorldLibraryController::worldTypeOf(worldType);
        if (type == QcWorldLibraryController::WorldType::Terrain && worldType != QLatin1StringView(QcWorldGenerators::terrain)) {
            qCWarning(lcStartup) << "Test hook: unknown world type" << worldType;
        } else {
            preselect(root, u"worldTypeChoice"_s, u"startWorldType"_s, static_cast<int>(type));
        }
    }
    if (!gameMode.isEmpty()) {
        const std::optional<QcGameMode> mode = QcGameModes::parse(gameMode);
        if (!mode) {
            qCWarning(lcStartup) << "Test hook: unknown game mode" << gameMode;
        } else {
            preselect(root, u"gameModeChoice"_s, u"startGameMode"_s, static_cast<int>(*mode));
        }
    }
}

} // namespace QcMenuPreselect

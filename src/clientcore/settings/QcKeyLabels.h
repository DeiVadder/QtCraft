#pragma once

#include "QcKeyBindings.h"

#include <QCoreApplication>
#include <QString>

// Texts the UI shows for keys and actions, in the UI language. Key names follow the platform: on
// macOS Qt::Key_Control is the Cmd key and Qt::Key_Meta the Control key.
class QcKeyLabels
{
    Q_DECLARE_TR_FUNCTIONS(QcKeyLabels)

public:
    // "V", "F5", "Space", "Cmd", ...; "–" for no key.
    static QString key(int key);
    // "Camera view", "Hotbar slot 3", ...
    static QString action(QcKeyBindings::Action action);
};

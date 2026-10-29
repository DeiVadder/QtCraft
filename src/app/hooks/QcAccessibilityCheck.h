// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

class QObject;
class QQuickItem;

// Test hook QTCRAFT_ACCESSIBILITY_CHECK=1 (screenshot runs, decision E-725): finds the shown
// controls a screen reader could not name. A control is a button, check box, switch, tab, slider,
// spin box, combo box, text field, list, or an item that takes clicks through a MouseArea or a
// TapHandler (slots, rows, links). Each needs an accessible name with a letter or a digit (its
// Accessible.name, for buttons also their text: "×" alone names nothing); combo boxes, text fields,
// sliders, spin boxes and lists need Accessible.name (their text is the value, not what it is for).
// An item that takes clicks needs Accessible.role as well (a button, a list item, a link …).
// Items marked Accessible.ignored, with their children, are left out (click catchers behind a
// screen).
// It reads the Accessible attached properties (private Qt Quick header): the public
// QAccessible::queryAccessibleInterface() needs an active screen reader (src/app/CMakeLists.txt).
class QcAccessibilityCheck
{
public:
    // One line per shown control under `root` without a usable name or role: type, object name,
    // what is missing and the scene rectangle.
    static QStringList unnamedControls(const QQuickItem &root);
    // The name a screen reader reads for `item`: Accessible.name, else a button's text; "" when
    // neither exists.
    static QString accessibleName(const QQuickItem &item);

private:
    [[nodiscard]] static QStringList check(const QQuickItem &item);
    // What `item` lacks as a control ("" when it is none or lacks nothing).
    static QString missing(const QQuickItem &item);
    static QString missingOnClickable(const QQuickItem &item);
    static bool inherits(const QObject &object, const char *className);
    static bool hasTapHandler(const QQuickItem &item);
    static bool ignored(const QQuickItem &item);
    static bool clickableRole(const QQuickItem &item);
    static bool meaningful(const QString &name);
};

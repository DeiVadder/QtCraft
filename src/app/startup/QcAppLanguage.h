// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

class QcGameController;
class QQmlApplicationEngine;
class QTranslator;
struct QcAppOptions;

// The language of the user interface and of the content names at start-up and when the settings
// switch it (decision E-530).
class QcAppLanguage
{
public:
    // --language or QTCRAFT_LANGUAGE override the language chosen in the settings, which overrides
    // the system language.
    static QString uiLanguage(const QcAppOptions &options);
    // Installs the UI translation for `name` ("" = system) and makes its locale the default for
    // numbers, dates and times; returns the language for content names.
    static QString install(QTranslator &translator, const QString &name);
    // Gives the game its content language and switches the language of the user interface
    // (translator and engine) and of the content when the settings ask for another one.
    static void followSettings(QcGameController &game, QQmlApplicationEngine &engine, QTranslator &translator, const QString &language);
};

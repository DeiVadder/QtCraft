// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/preview/QcPreviewHook.h"
#include "common/diagnostics/QcDiagnosticsOptions.h"

#include <QString>
#include <QStringList>

#include <optional>

class QGuiApplication;

// The command line of the game, with the environment variables that stand in for some of its
// options, and the checks beyond what QCommandLineParser does (decision E-530).
struct QcAppOptions
{
    bool play = false;
    // --world-type (or --showcase) and --flat-layers: the type of the world --play starts.
    QString worldType;
    QString flatLayers;
    // --game-mode: the player's mode in the world --play starts.
    QString gameMode;
    QString world;
    QString seed;
    std::optional<QString> camera;
    QString pose;
    std::optional<QString> look;
    std::optional<QString> spawn;
    QStringList chat;
    QStringList overlays;
    std::optional<QString> handbook;
    std::optional<QString> viewDistance;
    QString language;
    QString modsDirectory;
    bool noMods = false;
    bool noAudio = false;
    // --screen or QTCRAFT_SCREEN.
    QString screen;
    // --background or QTCRAFT_BACKGROUND=1.
    bool background = false;
    // --stats (or QTCRAFT_STATS), --debug-display and --debug-show (decision E-225).
    QString stats;
    std::optional<QString> debugDisplay;
    std::optional<QString> debugShow;
    // --preview: what every character preview shows (decision E-231).
    QcPreviewHook preview;
    // --join, --player-name, --password (or QTCRAFT_SERVER_PASSWORD) and --host-lan (decision E-251).
    std::optional<QString> join;
    QString playerName;
    QString password;
    std::optional<int> hostLan;
    // --log-file, --no-log-file, --log-rules, --health-log, --full-dump (decision E-834).
    QcDiagnosticsSetup diagnostics;
    // What is wrong with the command line beyond what QCommandLineParser checks.
    QString error;

    // The command line of `app`; handles --help and --version itself (and quits).
    static QcAppOptions parse(const QGuiApplication &app);
    // Why an offscreen run (E-229) cannot run as set up; empty when it can.
    static QString offscreenRunError();
};

#pragma once

#include "common/core/QcResult.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

// A folder to hand in after a playtest (decision E-834): the logs of the newest sessions and all
// crash reports and dumps of <data>/logs, settings.ini and server.json without secrets, and info.txt with the build, the
// system, the mods and the worlds of the data folder (and the files that could not be copied).
// Never the player key file, identities or worlds. A Windows memory dump holds memory of the
// process and so may hold a password typed in this session; the guide says so.
namespace QcDiagnosticsBundle {

// <data>/diagnostics: where the bundles go.
inline constexpr char folderName[] = "diagnostics";
// Sessions per program whose logs the bundle carries (crash reports and dumps: all).
inline constexpr int keptSessions = 3;

struct Contents
{
    QString program;
    QString dataDirectory;
    // Further lines for info.txt (the running world, the loaded mods).
    QStringList info;
};

// Writes the bundle into a new folder below <data>/diagnostics and returns its path.
[[nodiscard]] QcResult<QString> write(const Contents &contents);

// settings.ini without the values of keys that name a password, secret or token.
QByteArray cleanSettings(const QByteArray &ini);
// server.json with the values of such keys replaced by "<hidden>", at any depth.
QByteArray cleanServerSettings(const QByteArray &json);

} // namespace QcDiagnosticsBundle

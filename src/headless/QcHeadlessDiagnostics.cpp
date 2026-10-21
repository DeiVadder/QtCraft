#include "headless/QcHeadlessDiagnostics.h"

#include "common/diagnostics/QcDiagnosticsBundle.h"
#include "common/diagnostics/QcDiagnosticsSession.h"

#include <QCoreApplication>
#include <QDir>

#include <cstdio>
#include <print>

using namespace Qt::StringLiterals;

namespace QcHeadlessDiagnostics {

std::unique_ptr<QcDiagnosticsSession> start(const QCommandLineParser &parser, const QcHeadlessOptions::Options &options)
{
    const QcResult<QcDiagnosticsSetup> setup = options.diagnostics.read(parser);
    if (!setup) {
        QcHeadlessOptions::printError(setup.error());
        return nullptr;
    }
    return std::make_unique<QcDiagnosticsSession>(QcDiagnosticsSession::Start{
        .program = u"qtcraft-headless"_s,
        .dataDirectory = QcHeadlessOptions::dataDirectory(parser, options),
        .logByDefault = parser.isSet(options.server),
        .setup = *setup,
        .arguments = QCoreApplication::arguments(),
    });
}

bool writeBundle(const QString &dataDirectory, const QStringList &info)
{
    const QcResult<QString> written = QcDiagnosticsBundle::write({u"qtcraft-headless"_s, dataDirectory, info});
    if (!written) {
        QcHeadlessOptions::printError(u"Cannot write the diagnostics: "_s + written.error());
        return false;
    }
    std::println("Diagnostics written to {}", QDir::toNativeSeparators(*written).toStdString());
    std::fflush(stdout);
    return true;
}

} // namespace QcHeadlessDiagnostics

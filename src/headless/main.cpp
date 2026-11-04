// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "headless/QcHeadlessGame.h"
#include "network/tls/QcTlsBackend.h"

#include <QCoreApplication>

using namespace Qt::StringLiterals;

int main(int argc, char *argv[])
{
#if defined(Q_OS_WIN)
    // The log belongs on standard error (docs/Steuerschnittstelle.md); without a console window (a
    // service, a script or a test that reads the pipe) Qt would send it to the debugger instead.
    qputenv("QT_FORCE_STDERR_LOGGING", "1");
#endif
    const QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(u"qtcraft-headless"_s);
    QCoreApplication::setOrganizationName(u"QtCraft"_s);
    // OpenSSL at the start, so a broken installation shows at once (logged; servers then refuse to listen).
    // ensure() logs a failure itself.
    [[maybe_unused]] const QcStatus tls = QcTlsBackend::ensure();
    QcHeadlessGame game;
    if (!game.start(QCoreApplication::arguments())) {
        return 1;
    }
    return QCoreApplication::exec();
}

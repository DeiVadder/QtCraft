// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "headless/QcHeadlessGame.h"
#include "network/tls/QcTlsBackend.h"

#include <QCoreApplication>

using namespace Qt::StringLiterals;

int main(int argc, char *argv[])
{
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

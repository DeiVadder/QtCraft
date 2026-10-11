#pragma once

#include <QAbstractSocket>
#include <QString>

// Translated texts for socket errors (context "QcNetwork", decision E-261): Qt's own error strings
// are English and name system calls; players see what went wrong and what to check.
namespace QcSocketErrors {

// `fallback` (the socket's errorString()) for errors without a text of their own.
QString describe(QAbstractSocket::SocketError error, const QString &fallback);

} // namespace QcSocketErrors

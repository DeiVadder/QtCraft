#include "QcSocketErrors.h"

#include <QCoreApplication>

namespace QcSocketErrors {

QString describe(QAbstractSocket::SocketError error, const QString &fallback)
{
    switch (error) {
    case QAbstractSocket::ConnectionRefusedError:
        return QCoreApplication::translate("QcNetwork", "Nobody accepts connections at this address and port (is the server running?)");
    case QAbstractSocket::RemoteHostClosedError:
        return QCoreApplication::translate("QcNetwork", "The other side closed the connection");
    case QAbstractSocket::HostNotFoundError:
        return QCoreApplication::translate("QcNetwork", "The server address was not found");
    case QAbstractSocket::SocketTimeoutError:
        return QCoreApplication::translate("QcNetwork", "The server did not answer in time");
    case QAbstractSocket::NetworkError:
        return QCoreApplication::translate("QcNetwork", "The network cannot be reached");
    case QAbstractSocket::SocketAccessError:
        return QCoreApplication::translate("QcNetwork", "The system does not allow this connection");
    default:
        return fallback;
    }
}

} // namespace QcSocketErrors

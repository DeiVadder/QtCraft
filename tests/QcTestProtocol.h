#pragma once

#include "common/protocol/QcProtocol.h"
#include "common/protocol/QcProtocolCodec.h"

#include <QDataStream>

// Frames as clients of older protocol versions send them, written byte by byte so they do not
// follow the current encoder.
namespace QcTestProtocol {

// The Hello of a client of the version before the client text (decision E-834): version, player
// name, appearance.
inline QByteArray helloWithoutClientText(const QString &name)
{
    using QcProtocolCodec::operator<<;
    QByteArray frame;
    QDataStream stream(&frame, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_8);
    stream << static_cast<quint16>(QcMessage(QcMsg::Hello{}).index()) << (QcProtocol::firstVersionWithClientText - 1) << name << QcAppearance{};
    return frame;
}

} // namespace QcTestProtocol

// Payloads of the sign messages (QcSignMessages.h, decision E-766): a position and four lines, each
// at most QcSignText::maxLineUnits UTF-16 units on the wire.

#include "common/protocol/QcProtocolCodec.h"

using namespace Qt::StringLiterals;

namespace QcProtocolCodec {

namespace {

void writeSign(QDataStream &stream, QcBlockPos pos, const QcSignText::Lines &lines)
{
    writePos(stream, pos);
    for (const QString &line : lines) {
        stream << line;
    }
}

struct ReadSign
{
    QcBlockPos pos;
    QcSignText::Lines lines;
};

[[nodiscard]] QcResult<ReadSign> readSign(QDataStream &stream)
{
    ReadSign sign{readPos(stream), {}};
    for (QString &line : sign.lines) {
        stream >> line;
    }
    if (!QcSignText::fitsWire(sign.lines)) {
        return qcError(u"Sign line too long"_s);
    }
    return sign;
}

} // namespace

void encode(QDataStream &stream, const QcMsg::SignText &m)
{
    writeSign(stream, m.pos, m.lines);
}

void encode(QDataStream &stream, const QcMsg::OpenSignEditor &m)
{
    writeSign(stream, m.pos, m.lines);
}

void encode(QDataStream &stream, const QcMsg::EditSign &m)
{
    writeSign(stream, m.pos, m.lines);
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::SignText>, QDataStream &stream)
{
    QcResult<ReadSign> sign = readSign(stream);
    return sign ? QcResult<QcMessage>(QcMsg::SignText{sign->pos, sign->lines}) : qcError(sign.error());
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::OpenSignEditor>, QDataStream &stream)
{
    QcResult<ReadSign> sign = readSign(stream);
    return sign ? QcResult<QcMessage>(QcMsg::OpenSignEditor{sign->pos, sign->lines}) : qcError(sign.error());
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::EditSign>, QDataStream &stream)
{
    QcResult<ReadSign> sign = readSign(stream);
    return sign ? QcResult<QcMessage>(QcMsg::EditSign{sign->pos, sign->lines}) : qcError(sign.error());
}

} // namespace QcProtocolCodec

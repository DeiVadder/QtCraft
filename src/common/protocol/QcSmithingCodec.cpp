// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Payload of the smithing table's messages (QcSmithingMessages.h, decision E-1115).

#include "common/protocol/QcProtocolCodec.h"

namespace QcProtocolCodec {

void encode(QDataStream &stream, QcMsg::OpenSmithing m)
{
    writePos(stream, m.pos);
}

void encode(QDataStream &stream, const QcMsg::SmithingState &m)
{
    writeStack(stream, m.templateStack);
    writeStack(stream, m.base);
    writeStack(stream, m.addition);
    writeStack(stream, m.output);
}

void encode(QDataStream &stream, const QcMsg::ForgeSmithing &m)
{
    writeStack(stream, m.output);
}

void encode(QDataStream &, QcMsg::CloseSmithing) {}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::OpenSmithing>, QDataStream &stream)
{
    QcMsg::OpenSmithing m;
    m.pos = readPos(stream);
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::SmithingState>, QDataStream &stream)
{
    QcMsg::SmithingState m;
    m.templateStack = readStack(stream);
    m.base = readStack(stream);
    m.addition = readStack(stream);
    m.output = readStack(stream);
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ForgeSmithing>, QDataStream &stream)
{
    QcMsg::ForgeSmithing m;
    m.output = readStack(stream);
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CloseSmithing>, QDataStream &)
{
    return QcMsg::CloseSmithing{};
}

} // namespace QcProtocolCodec

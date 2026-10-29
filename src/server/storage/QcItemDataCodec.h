// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcItemStack.h"

#include <QDataStream>

// The data of an item stack in save files (decision E-823): the components of QcItemData framed like
// the components of entities (QcComponentFrames, decision E-420), so a version keeps what it does
// not know. A stack in a save is `name, count, damage`; when the count has dataFlag set, its data
// follows (QcSaveFile::writeStack, QcStackReader). Files without data read as before.
namespace QcItemDataCodec {

// Set in the saved count of a stack whose data follows; counts never come close.
inline constexpr qint32 dataFlag = 0x4000'0000;
// The version every known component writes.
inline constexpr quint16 version = 1;
// Names longer than this are damaged data; up to it a name is cut to QcItemData::maxNameLength
// (a later version may allow longer ones).
inline constexpr qsizetype maxSavedName = 1024;

// Writes the components of `data`: the map id (quint32), the contents (quint16 count, then the
// stacks by QcSaveFile::writeStack), the name, then the kept ones.
void write(QDataStream &stream, const QcItemData &data, const QcContentRegistry &content);

} // namespace QcItemDataCodec

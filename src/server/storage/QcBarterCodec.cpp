// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/storage/QcBarterCodec.h"

#include "server/storage/QcSaveFile.h"
#include "server/storage/QcStackReader.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace {

// The most stacks a saved barter state may keep (QcBarterDefinition::hoardSize is at most 36).
constexpr qint32 maxHoard = 36;

} // namespace

namespace QcBarterCodec {

std::optional<QcBarterState> read(QDataStream &in, QcStackReader &stacks)
{
    QcBarterState barter;
    barter.admiring = stacks.read(in);
    qint32 kept = 0;
    in >> barter.admireSeconds >> kept;
    if (kept < 0 || kept > maxHoard || !std::isfinite(barter.admireSeconds)) {
        return std::nullopt;
    }
    barter.admireSeconds = barter.admires() ? std::max(0.0, barter.admireSeconds) : 0.0;
    for (qint32 i = 0; i < kept; ++i) {
        // An item the content no longer has is lost, the reader warns of it.
        if (const QcItemStack stack = stacks.read(in); !stack.isEmpty()) {
            barter.hoard.append(stack);
        }
    }
    return barter;
}

void write(QDataStream &out, const QcBarterState &barter, const QcContentRegistry &content)
{
    QcSaveFile::writeStack(out, barter.admiring, content);
    out << barter.admireSeconds;
    QList<QcItemStack> kept;
    std::ranges::copy_if(barter.hoard, std::back_inserter(kept), [](const QcItemStack &stack) { return !stack.isEmpty(); });
    out << static_cast<qint32>(kept.size());
    for (const QcItemStack &stack : std::as_const(kept)) {
        QcSaveFile::writeStack(out, stack, content);
    }
}

} // namespace QcBarterCodec

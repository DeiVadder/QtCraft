#pragma once

#include "QcCircuitState.h"
#include "QcResult.h"

#include <QString>

// The circuits of a world in signals.qce (decision E-335): scheduled ticks with the ticks they still
// wait, block changes not yet looked at, burnt-out torches, openers last seen powered and pistons in
// the middle of extending, last the cooldowns of hoppers (a file that ends before them reads with
// none). Ticks are stored relative to the save, so a clock goes on where it stopped. No file means quiet circuits. Every list is written with at most maxEntries entries, the
// limit the reader accepts, so a world always reads its own save.
namespace QcSignalStorage {

// Entries per list at most.
inline constexpr quint32 maxEntries = 1u << 20;

QcStatus write(const QString &directory, const QcCircuitState &circuits, quint64 now);
// Fills `circuits` (cleared first) as if the saved ticks had passed up to `now`.
QcStatus read(const QString &directory, QcCircuitState &circuits, quint64 now);

} // namespace QcSignalStorage

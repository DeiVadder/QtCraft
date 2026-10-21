#pragma once

#include "common/diagnostics/QcHealthSample.h"

class QcClientSession;

// The client session's part of the health line (decision E-834).
namespace QcClientHealth {

// Loaded chunks, entities and players the client knows, the server's tick times when it sends debug
// data (the latest five seconds), the round trip of the inputs since the previous call and the
// bytes of the connection. Takes the round trips (QcLinkStats::take).
QcHealthSample sample(const QcClientSession &session);

} // namespace QcClientHealth

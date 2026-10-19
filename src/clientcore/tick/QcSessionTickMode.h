#pragma once

// How a client session ticks (decision E-560).
enum class QcSessionTickMode {
    // At 20 Hz on the session's tick thread (in the session's thread with QTCRAFT_SESSION_THREAD=0):
    // a long frame holds up neither the prediction nor the network. The graphical client.
    Threaded,
    // At 20 Hz in the session's thread: every tick is applied before the next one runs, so counts of
    // ticks are exact (headless client and control interface; no frames hold the thread up).
    Timer,
    // Only when the caller asks (QcClientSession::tick, tests), in the session's thread.
    Manual,
};

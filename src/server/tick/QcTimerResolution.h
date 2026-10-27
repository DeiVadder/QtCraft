#pragma once

// Asks the system for timers to the millisecond while it lives. Windows otherwise wakes timers and
// sleeps in steps of its scheduler tick (about 15.6 ms), so the server's ticks would start 15-30 ms
// late; macOS and Linux need nothing (decision E-901).
class QcTimerResolution
{
public:
    QcTimerResolution() = default;
    ~QcTimerResolution();

    QcTimerResolution(const QcTimerResolution &) = delete;
    QcTimerResolution &operator=(const QcTimerResolution &) = delete;

private:
    // Whether the request was granted (always false outside Windows).
    static bool raise();

    [[maybe_unused]] bool m_raised = raise();
};

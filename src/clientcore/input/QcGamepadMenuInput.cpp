#include "clientcore/input/QcGamepadMenuInput.h"

#include "clientcore/input/QcGamepadSource.h"

#include <array>
#include <cmath>
#include <utility>

namespace {

using Command = QcGamepadMenuInput::Command;

// The buttons that are commands, and theirs.
constexpr std::array<std::pair<QcGamepadButton, Command>, 7> buttonCommands{{
    {QcGamepadButton::South, Command::Confirm},
    {QcGamepadButton::East, Command::Back},
    {QcGamepadButton::West, Command::Secondary},
    {QcGamepadButton::North, Command::Tertiary},
    {QcGamepadButton::Start, Command::Start},
    {QcGamepadButton::LeftShoulder, Command::TabPrevious},
    {QcGamepadButton::RightShoulder, Command::TabNext},
}};

// The right stick scrolls: its y up and down, its x sideways (a wide page such as the advancement tree).
float scrollAxis(const QcGamepadState &raw, QcGamepadAxis axis = QcGamepadAxis::RightY)
{
    const float value = raw.axis(axis);
    return std::abs(value) > QcGamepadMenuInput::scrollDeadZone ? value : 0.0f;
}

bool scrolls(const QcGamepadState &raw)
{
    return scrollAxis(raw) != 0.0f || scrollAxis(raw, QcGamepadAxis::RightX) != 0.0f;
}

} // namespace

QcGamepadMenuInput::QcGamepadMenuInput(QObject *parent)
    : QObject(parent)
{
    m_clock.start();
    m_timer.setInterval(timerIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, [this] { advance(m_clock.elapsed()); });
}

QcGamepadMenuInput::~QcGamepadMenuInput() = default;

void QcGamepadMenuInput::setSource(QcGamepadSource *source)
{
    if (m_source == source) {
        return;
    }

    if (m_source) {
        disconnect(m_source, nullptr, this, nullptr);
    }
    m_source = source;
    m_states.clear();
    m_held.reset();
    m_heldPad = 0;
    m_waitForNeutral = false;
    if (m_source) {
        connect(m_source, &QcGamepadSource::stateChanged, this, &QcGamepadMenuInput::onStateChanged);
        connect(m_source, &QcGamepadSource::padDisconnected, this, &QcGamepadMenuInput::onPadDisconnected);
    }
    updateTimer();
}

std::optional<QcGamepadMenuInput::Command> QcGamepadMenuInput::direction(const QcGamepadState &raw)
{
    if (raw.pressed(QcGamepadButton::DpadUp)) {
        return Command::Up;
    }
    if (raw.pressed(QcGamepadButton::DpadDown)) {
        return Command::Down;
    }
    if (raw.pressed(QcGamepadButton::DpadLeft)) {
        return Command::Left;
    }
    if (raw.pressed(QcGamepadButton::DpadRight)) {
        return Command::Right;
    }

    const float x = raw.axis(QcGamepadAxis::LeftX);
    const float y = raw.axis(QcGamepadAxis::LeftY);
    if (std::hypot(x, y) < stickDirection) {
        return std::nullopt;
    }
    if (std::abs(x) > std::abs(y)) {
        return x > 0.0f ? Command::Right : Command::Left;
    }
    return y > 0.0f ? Command::Down : Command::Up;
}

void QcGamepadMenuInput::onStateChanged(int id)
{
    if (!m_source) {
        return;
    }

    const QcGamepadState now = m_source->state(id);
    const QcGamepadState before = m_states.value(id);
    m_states.insert(id, now);
    emitPresses(before, now);
    if (!gateOpen()) {
        closeGate();
        return;
    }
    if (m_waitForNeutral) {
        m_waitForNeutral = !neutral();
        updateTimer();
        return;
    }

    // A new direction acts at once and repeats while held; letting go of the holding pad stops it.
    const std::optional<Command> was = direction(before);
    const std::optional<Command> is = direction(now);
    if (is && is != was) {
        m_held = is;
        m_heldPad = id;
        m_nextRepeatMs = m_clock.elapsed() + repeatDelayMs;
        emit command(*is);
    } else if (!is && id == m_heldPad) {
        m_held.reset();
        m_heldPad = 0;
    }
    updateTimer();
}

void QcGamepadMenuInput::emitPresses(const QcGamepadState &before, const QcGamepadState &now)
{
    for (const auto &[button, buttonCommand] : buttonCommands) {
        if (now.pressed(button) && !before.pressed(button)) {
            emit command(buttonCommand);
        }
    }
}

void QcGamepadMenuInput::onPadDisconnected(int id)
{
    m_states.remove(id);
    if (id == m_heldPad) {
        m_held.reset();
        m_heldPad = 0;
    }
    updateTimer();
}

void QcGamepadMenuInput::closeGate()
{
    m_held.reset();
    m_heldPad = 0;
    m_waitForNeutral = !neutral();
    m_timer.stop();
}

bool QcGamepadMenuInput::neutral() const
{
    for (const QcGamepadState &state : std::as_const(m_states)) {
        if (direction(state) || scrolls(state)) {
            return false;
        }
    }
    return true;
}

void QcGamepadMenuInput::advance(qint64 nowMs)
{
    const qint64 elapsedMs = std::max<qint64>(0, nowMs - m_lastAdvanceMs);
    m_lastAdvanceMs = nowMs;
    if (!gateOpen()) {
        closeGate();
        return;
    }
    if (m_waitForNeutral) {
        return;
    }
    while (m_held && nowMs >= m_nextRepeatMs) {
        m_nextRepeatMs += repeatIntervalMs;
        emit command(*m_held);
    }

    float scroll = 0.0f;
    float sideways = 0.0f;
    for (const QcGamepadState &state : std::as_const(m_states)) {
        scroll += scrollAxis(state);
        sideways += scrollAxis(state, QcGamepadAxis::RightX);
    }
    const float seconds = static_cast<float>(std::min<qint64>(elapsedMs, 100)) / 1000.0f;
    if (scroll != 0.0f) {
        emit scrolled(scroll * scrollSpeed * seconds);
    }
    if (sideways != 0.0f) {
        emit scrolledSideways(sideways * scrollSpeed * seconds);
    }
}

void QcGamepadMenuInput::updateTimer()
{
    bool scrolling = false;
    for (const QcGamepadState &state : std::as_const(m_states)) {
        scrolling = scrolling || scrolls(state);
    }

    const bool needed = !m_waitForNeutral && (m_held.has_value() || scrolling);
    if (needed && !m_timer.isActive()) {
        m_lastAdvanceMs = m_clock.elapsed();
        m_timer.start();
    } else if (!needed) {
        m_timer.stop();
    }
}

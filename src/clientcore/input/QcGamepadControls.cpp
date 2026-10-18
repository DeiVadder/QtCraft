#include "clientcore/input/QcGamepadControls.h"

#include "clientcore/camera/QcMouseLook.h"
#include "clientcore/input/QcGamepadSource.h"
#include "clientcore/session/QcClientSession.h"
#include "common/gameplay/QcInventory.h"

namespace {

// Sets `field` while the pad wants it and gives it back (false) once when it stops; `mine` says
// whether the pad set it.
void contribute(bool &field, bool want, bool &mine)
{
    if (want) {
        field = true;
        mine = true;
    } else if (mine) {
        field = false;
        mine = false;
    }
}

} // namespace

QcGamepadControls::QcGamepadControls(QObject *parent)
    : QObject(parent)
{
    m_clock.start();
}

QcGamepadControls::~QcGamepadControls() = default;

void QcGamepadControls::setSource(QcGamepadSource *source)
{
    if (m_source == source) {
        return;
    }

    if (m_source) {
        disconnect(m_source, nullptr, this, nullptr);
    }
    m_source = source;
    m_held.clear();
    m_presses.clear();
    m_pad = 0;
    if (!m_source) {
        return;
    }

    connect(m_source, &QcGamepadSource::stateChanged, this, &QcGamepadControls::onStateChanged);
    connect(m_source, &QcGamepadSource::padConnected, this, &QcGamepadControls::onPadConnected);
    connect(m_source, &QcGamepadSource::padDisconnected, this, &QcGamepadControls::onPadDisconnected);
    const QList<QcGamepadInfo> pads = m_source->pads();
    m_pad = pads.isEmpty() ? 0 : pads.first().id;
}

void QcGamepadControls::setSettings(const QcGamepadSettings &settings)
{
    m_settings = settings;
    m_settings.response = settings.response.sanitized();
    m_settings.lookSpeed = QcGamepadSettings::clampLookSpeed(settings.lookSpeed);
}

QcGamepadState QcGamepadControls::shapedState(int id) const
{
    return m_source ? QcGamepadCurve::apply(m_source->state(id), m_settings.response) : QcGamepadState();
}

void QcGamepadControls::onPadConnected(int id)
{
    if (m_pad == 0) {
        m_pad = id;
    }
}

void QcGamepadControls::onPadDisconnected(int id)
{
    if (id == m_pad) {
        releaseHeld(id);
        m_pad = 0;
        m_sneakToggled = false;
    }
    m_held.remove(id);
}

void QcGamepadControls::releaseHeld(int id)
{
    const qint64 now = m_clock.elapsed();
    const Held held = m_held.value(id);
    for (std::size_t index = 0; index < held.size(); ++index) {
        if (held[index]) {
            queue(Action(index), false, now);
        }
    }
}

void QcGamepadControls::queue(Action action, bool pressed, qint64 timeMs)
{
    if (!m_active) {
        return;
    }

    if (m_presses.size() >= maxPresses) {
        m_presses.removeFirst();
    }
    m_presses.append({action, pressed, timeMs});
}

bool QcGamepadControls::takesLead(int id, Held before, Held now) const
{
    for (std::size_t index = 0; index < now.size(); ++index) {
        if (now[index] && !before[index]) {
            return true;
        }
    }

    // A drifting stick never takes the lead, and a pushed one not from a pad that walks or looks.
    if (!m_source || !QcGamepadMapping::clearlyDeflected(m_source->state(id))) {
        return false;
    }
    return m_pad == 0 || !QcGamepadMapping::deflected(shapedState(m_pad));
}

void QcGamepadControls::onStateChanged(int id)
{
    const QcGamepadState shaped = shapedState(id);
    const Held before = m_held.value(id);
    Held now{};
    for (std::size_t index = 0; index < now.size(); ++index) {
        now[index] = QcGamepadMapping::held(Action(index), shaped, m_bindings, before[index]);
    }

    if (id != m_pad && takesLead(id, before, now)) {
        if (m_pad != 0) {
            releaseHeld(m_pad);
        }
        m_pad = id;
    }

    if (id == m_pad) {
        const qint64 timeMs = m_clock.elapsed();
        for (std::size_t index = 0; index < now.size(); ++index) {
            if (now[index] != before[index]) {
                queue(Action(index), now[index], timeMs);
            }
        }
    }
    m_held.insert(id, now);
}

void QcGamepadControls::applyFrame(QcClientSession &session, float seconds, bool active)
{
    if (session.isDead()) {
        m_sneakToggled = false;
    }

    if (!active) {
        if (m_active) {
            release(session);
        }
        m_presses.clear();
        return;
    }

    m_active = true;
    const bool sprintPressed = runPresses(session);
    const QcGamepadState shaped = shapedState(m_pad);
    QcPlayerInput intent = session.intent();
    applyWalk(intent, shaped, sprintPressed, session.gameMode().flightWanted());

    const QPointF look = QcGamepadMapping::lookDelta(shaped, m_settings.lookSpeed, double(seconds));
    if (!look.isNull()) {
        intent = QcMouseLook::apply(intent, look, 1.0, m_settings.invertLook);
    }
    if (intent != session.intent()) {
        session.setIntent(intent);
    }
}

bool QcGamepadControls::runPresses(QcClientSession &session)
{
    bool sprintPressed = false;
    const QList<Press> presses = std::exchange(m_presses, {});
    for (const Press &press : presses) {
        sprintPressed = sprintPressed || (press.action == Action::Sprint && press.pressed);
        runPress(session, press);
    }
    return sprintPressed;
}

void QcGamepadControls::runPress(QcClientSession &session, Press press)
{
    switch (press.action) {
    case Action::Jump:
        if (press.pressed && m_flight.jumpPressed(press.timeMs)) {
            session.gameMode().toggleFlight();
        }
        return;
    case Action::Sneak:
        // On the ground a click toggles sneaking; in flight the button is held to descend.
        if (press.pressed && !session.gameMode().flightWanted()) {
            m_sneakToggled = !m_sneakToggled;
        }
        return;
    case Action::Mine:
        // Like the left mouse button: a press on a creature hits it, holding on a block mines. Only
        // the pad's own mining ends with the trigger, not the mouse's.
        if (press.pressed && !session.attack()) {
            session.setMining(true);
            m_mining = true;
        } else if (!press.pressed && m_mining) {
            session.setMining(false);
            m_mining = false;
        }
        return;
    case Action::Use:
        if (press.pressed) {
            session.useItem();
            m_using = true;
        } else if (m_using) {
            session.releaseUse();
            m_using = false;
        }
        return;
    case Action::HotbarPrevious:
    case Action::HotbarNext:
        if (press.pressed) {
            const int step = press.action == Action::HotbarNext ? 1 : -1;
            session.selectHotbarSlot(QcGamepadMapping::hotbarStep(session.inventory().selectedHotbarSlot(), step, QcInventory::hotbarSize));
        }
        return;
    case Action::Sprint:
    case Action::Count:
        return;
    }
}

void QcGamepadControls::applyWalk(QcPlayerInput &intent, const QcGamepadState &shaped, bool sprintPressed, bool flying)
{
    using QcGamepadMapping::held;
    const QcGamepadMapping::Walk walk = QcGamepadMapping::walk(shaped);
    if (walk.moving()) {
        intent.forward = walk.forward;
        intent.strafe = walk.strafe;
        m_walking = true;
    } else if (m_walking) {
        intent.forward = 0.0f;
        intent.strafe = 0.0f;
        m_walking = false;
    }

    // Sprinting ends a toggled sneak, as in the model.
    if (sprintPressed && !flying) {
        m_sneakToggled = false;
    }
    const bool sneak = flying ? held(Action::Sneak, shaped, m_bindings, false) : m_sneakToggled;
    const bool sprintHeld = sprintPressed || held(Action::Sprint, shaped, m_bindings, false);
    const bool sprint = m_sprint.update(walk.forward >= QcGamepadMapping::sprintForward, sprintHeld, sneak);
    contribute(intent.jump, held(Action::Jump, shaped, m_bindings, false), m_jumping);
    contribute(intent.sneak, sneak, m_sneaking);
    contribute(intent.sprint, sprint, m_sprinting);
}

void QcGamepadControls::release(QcClientSession &session)
{
    if (m_mining) {
        session.setMining(false);
    }
    if (m_using) {
        // Like a screen opening: a drawn bow goes without a shot.
        session.cancelUse();
    }

    QcPlayerInput intent = session.intent();
    if (m_walking) {
        intent.forward = 0.0f;
        intent.strafe = 0.0f;
    }
    contribute(intent.jump, false, m_jumping);
    contribute(intent.sneak, false, m_sneaking);
    contribute(intent.sprint, false, m_sprinting);
    if (intent != session.intent()) {
        session.setIntent(intent);
    }
    stop();
}

void QcGamepadControls::suspend(QcClientSession *session)
{
    if (m_active && session) {
        release(*session);
    }
    stop();
}

void QcGamepadControls::reset()
{
    stop();
    m_sneakToggled = false;
}

void QcGamepadControls::stop()
{
    m_presses.clear();
    m_sprint.reset();
    m_flight.reset();
    m_active = false;
    m_walking = false;
    m_jumping = false;
    m_sneaking = false;
    m_sprinting = false;
    m_mining = false;
    m_using = false;
}

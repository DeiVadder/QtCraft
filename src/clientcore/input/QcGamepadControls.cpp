#include "clientcore/input/QcGamepadControls.h"

#include "clientcore/camera/QcMouseLook.h"
#include "clientcore/input/QcGamepadSource.h"
#include "clientcore/session/QcClientSession.h"
#include "common/gameplay/QcInventory.h"

#include <algorithm>

bool QcGamepadControls::SharedFlag::merged(bool current, bool want)
{
    if (want) {
        m_mine = true;
        return true;
    }
    if (m_mine) {
        m_mine = false;
        return false;
    }
    return current;
}

QcGamepadControls::QcGamepadControls(QObject *parent)
    : QObject(parent)
    , m_radial(QcInventory::hotbarSize)
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
    refreshProfiles();
    if (!m_source) {
        setLead(0);
        return;
    }

    connect(m_source, &QcGamepadSource::stateChanged, this, &QcGamepadControls::onStateChanged);
    connect(m_source, &QcGamepadSource::padConnected, this, &QcGamepadControls::onPadConnected);
    connect(m_source, &QcGamepadSource::padDisconnected, this, &QcGamepadControls::onPadDisconnected);
    refreshProfiles();
    const QList<QcGamepadInfo> pads = m_source->pads();
    setLead(pads.isEmpty() ? 0 : pads.first().id);
}

void QcGamepadControls::refreshProfiles()
{
    m_profiles.clear();
    if (!m_source) {
        return;
    }

    const QList<QcGamepadInfo> pads = m_source->pads();
    for (const QcGamepadInfo &pad : pads) {
        m_profiles.insert(pad.id, &m_settings.profileFor(pad.guid));
    }
}

void QcGamepadControls::setSettings(const QcGamepadSettings &settings)
{
    m_settings = settings;
    m_settings.standard = settings.standard.sanitized();
    refreshProfiles();
    m_profile = profileOf(m_pad);
    emit profileChanged();
}

const QcGamepadProfile &QcGamepadControls::profileOf(int id) const
{
    const QcGamepadProfile *profile = m_profiles.value(id);
    return profile ? *profile : m_settings.standard;
}

void QcGamepadControls::setLead(int id)
{
    const bool changed = id != m_pad;
    m_pad = id;
    m_profile = profileOf(id);
    if (changed) {
        // The new pad is in use once it is used (a takeover is a use: onStateChanged marks it).
        m_inUse = false;
        m_lastHealth = -1.0;
        emit profileChanged();
    }
}

QcGamepadState QcGamepadControls::shapedState(int id) const
{
    return m_source ? QcGamepadCurve::apply(m_source->state(id), profileOf(id).response) : QcGamepadState();
}

void QcGamepadControls::onPadConnected(int id)
{
    if (const std::optional<QcGamepadInfo> info = m_source ? m_source->info(id) : std::nullopt) {
        m_profiles.insert(id, &m_settings.profileFor(info->guid));
    }
    if (m_pad == 0) {
        setLead(id);
    }
}

void QcGamepadControls::onPadDisconnected(int id)
{
    if (id == m_pad) {
        releaseHeld(id);
        m_sneakToggled = false;
        // Without the pad he plays with the player cannot go on: the pause menu opens, as on a
        // console. A pad that only lies connected beside the keyboard and mouse pauses nothing.
        if (m_active && m_inUse) {
            emit worldAction(QcKeyBindings::Action::Pause, true);
        }
        setLead(0);
    }
    m_held.remove(id);
    m_profiles.remove(id);
}

void QcGamepadControls::releaseHeld(int id)
{
    const qint64 now = m_clock.elapsed();
    const Held held = m_held.value(id);
    for (std::size_t index = 0; index < held.size(); ++index) {
        if (held[index]) {
            queue(static_cast<Action>(index), false, now);
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
    const QcGamepadProfile &profile = profileOf(id);
    const Held before = m_held.value(id);
    Held now{};
    for (std::size_t index = 0; index < now.size(); ++index) {
        now[index] = QcGamepadMapping::held(static_cast<Action>(index), shaped, profile.bindings, before[index], profile.triggerThreshold);
    }
    now = QcGamepadMapping::withCombos(now, before, shaped, profile.bindings);

    if (id != m_pad && takesLead(id, before, now)) {
        if (m_pad != 0) {
            releaseHeld(m_pad);
        }
        setLead(id);
    }

    if (id == m_pad) {
        m_inUse = m_inUse || (m_source && QcGamepadMapping::used(m_source->state(id)));
        const qint64 timeMs = m_clock.elapsed();
        for (std::size_t index = 0; index < now.size(); ++index) {
            if (now[index] != before[index]) {
                queue(static_cast<Action>(index), now[index], timeMs);
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
    rumbleWhenHurt(session);

    if (!active) {
        if (m_active) {
            release(session);
        }
        m_presses.clear();
        return;
    }

    m_active = true;
    const bool sprintPressed = runPresses(session);
    dropStackWhenHeld(session);
    const QcGamepadState shaped = shapedState(m_pad);
    QcPlayerInput intent = walked(session.intent(), shaped, sprintPressed, session.gameMode().flightWanted());

    // While the radial menu is open, the right stick chooses a slot instead of looking.
    if (m_radial.isOpen()) {
        const QcGamepadState raw = m_source ? m_source->state(m_pad) : QcGamepadState();
        if (m_radial.point(raw.axis(QcGamepadAxis::RightX), raw.axis(QcGamepadAxis::RightY))) {
            emit radialChanged();
        }
    }
    QPointF look = m_radial.isOpen() ? QPointF() : QcGamepadMapping::lookDelta(shaped, m_profile.lookSpeed, static_cast<double>(seconds));
    m_lookWaitsForNeutral = (m_lookWaitsForNeutral || (m_radialWasOpen && !m_radial.isOpen())) && !look.isNull();
    m_radialWasOpen = m_radial.isOpen();
    if (m_lookWaitsForNeutral) {
        look = QPointF();
    }
    if (!look.isNull()) {
        intent = QcMouseLook::apply(intent, look, 1.0, m_profile.invertLook);
    }
    if (intent != session.intent()) {
        session.setIntent(intent);
    }
}

void QcGamepadControls::rumbleWhenHurt(const QcClientSession &session)
{
    // Only with a pad: without one this is a comparison per frame.
    if (m_pad == 0) {
        return;
    }

    if (!session.vitals().healthKnown()) {
        m_lastHealth = -1.0;
        return;
    }

    const double health = session.vitals().health();
    const double damage = m_lastHealth - health;
    m_lastHealth = health;
    if (damage > 0.0 && m_inUse && m_windowActive && m_profile.vibration && m_source) {
        const double strength = std::clamp(0.35 + 0.65 * damage / fullRumbleDamage, 0.0, 1.0);
        m_source->rumble(m_pad, static_cast<float>(strength), rumbleMs);
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
    if (runWorldPress(press) || runItemPress(session, press)) {
        return;
    }

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
    default:
        return;
    }
}

bool QcGamepadControls::runWorldPress(Press press)
{
    using Key = QcKeyBindings::Action;
    std::optional<Key> key;
    switch (press.action) {
    case Action::Pause:
        key = Key::Pause;
        break;
    case Action::Inventory:
        key = Key::Inventory;
        break;
    case Action::Chat:
        key = Key::Chat;
        break;
    case Action::Handbook:
        key = Key::Handbook;
        break;
    case Action::CameraView:
        key = Key::CameraView;
        break;
    case Action::HideHud:
        key = Key::HideHud;
        break;
    case Action::Screenshot:
        key = Key::Screenshot;
        break;
    case Action::PlayerList:
        // Held like its key: shown while the button is down.
        if (press.pressed != m_playerList) {
            m_playerList = press.pressed;
            emit worldAction(Key::PlayerList, press.pressed);
        }
        return true;
    default:
        return false;
    }

    if (press.pressed && key) {
        emit worldAction(*key, true);
    }
    return true;
}

bool QcGamepadControls::runItemPress(QcClientSession &session, Press press)
{
    switch (press.action) {
    case Action::Drop:
        m_dropPressedMs = press.pressed ? press.timeMs : -1;
        if (press.pressed) {
            session.dropItem(false);
        }
        return true;
    case Action::PickBlock:
        if (const std::optional<QcRaycastHit> &target = session.hand().target(); press.pressed && target && !session.isDead()) {
            session.gameMode().pickBlock(target->block);
        }
        return true;
    case Action::HotbarRadial:
        if (press.pressed) {
            m_radial.open(session.inventory().selectedHotbarSlot());
            emit radialChanged();
        } else {
            closeRadial(&session, true);
        }
        return true;
    default:
        return false;
    }
}

void QcGamepadControls::dropStackWhenHeld(QcClientSession &session)
{
    if (m_dropPressedMs >= 0 && m_clock.elapsed() - m_dropPressedMs >= QcGamepadMapping::dropStackMs) {
        m_dropPressedMs = -1;
        session.dropItem(true);
    }
}

void QcGamepadControls::closeRadial(QcClientSession *session, bool select)
{
    if (!m_radial.isOpen()) {
        return;
    }

    const std::optional<int> slot = m_radial.close();
    if (select && session && slot && *slot != session->inventory().selectedHotbarSlot()) {
        session->selectHotbarSlot(*slot);
    }
    emit radialChanged();
}

QcPlayerInput QcGamepadControls::walked(QcPlayerInput intent, const QcGamepadState &shaped, bool sprintPressed, bool flying)
{
    const QcGamepadMapping::Bindings &bindings = m_profile.bindings;
    const auto held = [&shaped, &bindings](Action action) { return QcGamepadMapping::held(action, shaped, bindings, false); };
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
    const bool sneak = flying ? held(Action::Sneak) : m_sneakToggled;
    const bool sprintHeld = sprintPressed || held(Action::Sprint);
    const bool sprint = m_sprint.update(walk.forward >= QcGamepadMapping::sprintForward, sprintHeld, sneak);
    intent.jump = m_jumping.merged(intent.jump, held(Action::Jump));
    intent.sneak = m_sneaking.merged(intent.sneak, sneak);
    intent.sprint = m_sprinting.merged(intent.sprint, sprint);
    return intent;
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
    intent.jump = m_jumping.merged(intent.jump, false);
    intent.sneak = m_sneaking.merged(intent.sneak, false);
    intent.sprint = m_sprinting.merged(intent.sprint, false);
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
    m_lastHealth = -1.0;
}

void QcGamepadControls::stop()
{
    // A screen opened or the pad stopped: the radial menu closes without choosing, the player list goes.
    closeRadial(nullptr, false);
    if (m_playerList) {
        m_playerList = false;
        emit worldAction(QcKeyBindings::Action::PlayerList, false);
    }
    m_dropPressedMs = -1;
    m_presses.clear();
    m_sprint.reset();
    m_flight.reset();
    m_active = false;
    m_walking = false;
    m_jumping = {};
    m_sneaking = {};
    m_sprinting = {};
    m_mining = false;
    m_using = false;
}

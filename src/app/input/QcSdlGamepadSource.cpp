#include "app/input/QcSdlGamepadSource.h"

#include "clientcore/input/QcGamepadTypeDetection.h"

#include <SDL3/SDL.h>

#include <QLoggingCategory>
#include <QThread>

#include <algorithm>
#include <array>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcGamepad, "qtcraft.gamepad")

// SDL's buttons in the order of QcGamepadButton.
constexpr std::array<SDL_GamepadButton, qcGamepadButtonCount> sdlButtons{
    SDL_GAMEPAD_BUTTON_SOUTH,          SDL_GAMEPAD_BUTTON_EAST,           SDL_GAMEPAD_BUTTON_WEST,
    SDL_GAMEPAD_BUTTON_NORTH,          SDL_GAMEPAD_BUTTON_BACK,           SDL_GAMEPAD_BUTTON_GUIDE,
    SDL_GAMEPAD_BUTTON_START,          SDL_GAMEPAD_BUTTON_LEFT_STICK,     SDL_GAMEPAD_BUTTON_RIGHT_STICK,
    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,  SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_DPAD_UP,
    SDL_GAMEPAD_BUTTON_DPAD_DOWN,      SDL_GAMEPAD_BUTTON_DPAD_LEFT,      SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
};

// SDL's axes in the order of QcGamepadAxis.
constexpr std::array<SDL_GamepadAxis, qcGamepadAxisCount> sdlAxes{
    SDL_GAMEPAD_AXIS_LEFTX,        SDL_GAMEPAD_AXIS_LEFTY,         SDL_GAMEPAD_AXIS_RIGHTX,
    SDL_GAMEPAD_AXIS_RIGHTY,       SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
};

QcGamepadType typeOf(SDL_Gamepad *gamepad, const QString &name)
{
    switch (SDL_GetGamepadType(gamepad)) {
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE:
        return QcGamepadType::Xbox;
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5:
        return QcGamepadType::PlayStation;
    // A single Joy-Con (SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT/RIGHT) has too few buttons
    // for the Switch Pro layout and ends as Generic through QcGamepadTypeDetection.
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return QcGamepadType::SwitchPro;
    default:
        return QcGamepadTypeDetection::detect(SDL_GetGamepadVendor(gamepad), SDL_GetGamepadProduct(gamepad), name);
    }
}

// SDL's GUID of the pad's model as 32 hex digits (the key of its profile, decision E-611).
QString guidOf(SDL_JoystickID id)
{
    std::array<char, 33> text{};
    SDL_GUIDToString(SDL_GetGamepadGUIDForID(id), text.data(), int(text.size()));
    return QString::fromLatin1(text.data());
}

} // namespace

QcSdlGamepadSource::QcSdlGamepadSource(int activeIntervalMs, QObject *parent)
    : QcGamepadSource(parent)
    , m_activeIntervalMs(activeIntervalMs)
{
    Q_ASSERT(QThread::isMainThread());

    // The game owns SIGINT/SIGTERM. Pads are read while the window is in the background too: the
    // input mapping (40.x) decides by the window's focus whether the input counts.
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        m_initError = QString::fromUtf8(SDL_GetError());
        qCWarning(lcGamepad) << "SDL gamepad subsystem did not start:" << m_initError;
        return;
    }

    // Nobody reads SDL's event queue: changes are found by polling. Events queued while starting
    // (devices present at start) are dropped.
    SDL_SetGamepadEventsEnabled(false);
    SDL_SetJoystickEventsEnabled(false);
    SDL_FlushEvents(SDL_EVENT_JOYSTICK_AXIS_MOTION, SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED);
    m_available = true;
    connect(&m_timer, &QTimer::timeout, this, &QcSdlGamepadSource::poll);
    m_timer.start(idleIntervalMs);
}

QcSdlGamepadSource::~QcSdlGamepadSource()
{
    if (!m_available) {
        return;
    }

    m_timer.stop();
    for (const OpenPad &pad : std::as_const(m_open)) {
        SDL_CloseGamepad(pad.gamepad);
    }
    m_open.clear();
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

QString QcSdlGamepadSource::backendName() const
{
    return u"sdl3"_s;
}

void QcSdlGamepadSource::poll()
{
    Q_ASSERT(QThread::isMainThread());
    if (!m_available) {
        return;
    }

    SDL_UpdateGamepads();
    closeGonePads();
    openNewPads();
    for (auto it = m_open.cbegin(); it != m_open.cend(); ++it) {
        updateState(it->id, readState(it->gamepad));
    }

    const int interval = m_open.isEmpty() ? idleIntervalMs : m_activeIntervalMs;
    if (m_timer.interval() != interval) {
        m_timer.setInterval(interval);
    }
}

bool QcSdlGamepadSource::rumble(int id, float strength, int durationMs)
{
    // SDL's pads are driven from the main thread only (the frames call this there).
    Q_ASSERT(QThread::isMainThread());
    const auto it = std::ranges::find_if(m_open, [id](OpenPad pad) { return pad.id == id; });
    const std::optional<QcGamepadInfo> pad = info(id);
    if (it == m_open.end() || !pad || !pad->rumble) {
        return false;
    }

    // Both motors alike: the strong (low frequency) and the weak one.
    const auto motor = Uint16(std::clamp(strength, 0.0f, 1.0f) * 65535.0f);
    return SDL_RumbleGamepad(it->gamepad, motor, motor, Uint32(std::max(durationMs, 0)));
}

void QcSdlGamepadSource::openNewPads()
{
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    QSet<quint32> present;
    for (int i = 0; ids && i < count; ++i) {
        present.insert(ids[i]);
        if (m_open.contains(ids[i]) || m_failed.contains(ids[i])) {
            continue;
        }

        SDL_Gamepad *gamepad = SDL_OpenGamepad(ids[i]);
        if (!gamepad) {
            qCWarning(lcGamepad) << "Could not open gamepad" << ids[i] << SDL_GetError();
            m_failed.insert(ids[i]);
            continue;
        }

        QcGamepadInfo info;
        info.name = QString::fromUtf8(SDL_GetGamepadName(gamepad));
        info.type = typeOf(gamepad, info.name);
        info.guid = guidOf(ids[i]);
        info.rumble = SDL_GetBooleanProperty(SDL_GetGamepadProperties(gamepad), SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
        qCInfo(lcGamepad) << "Gamepad connected:" << info.name << QcGamepadNames::type(info.type) << info.guid;
        m_open.insert(ids[i], OpenPad{.id = addPad(info), .gamepad = gamepad});
    }
    SDL_free(ids);
    m_failed.intersect(present);
}

void QcSdlGamepadSource::closeGonePads()
{
    for (auto it = m_open.begin(); it != m_open.end();) {
        if (SDL_GamepadConnected(it->gamepad)) {
            ++it;
            continue;
        }

        qCInfo(lcGamepad) << "Gamepad disconnected:" << it->id;
        SDL_CloseGamepad(it->gamepad);
        const int id = it->id;
        it = m_open.erase(it);
        removePad(id);
    }
}

QcGamepadState QcSdlGamepadSource::readState(SDL_Gamepad *gamepad) const
{
    QcGamepadState state;
    for (std::size_t i = 0; i < qcGamepadButtonCount; ++i) {
        state.setPressed(QcGamepadButton(i), SDL_GetGamepadButton(gamepad, sdlButtons[i]));
    }

    // SDL reports -32768..32767 for the sticks and 0..32767 for the triggers; setAxis clamps the
    // -32768 of a stick to -1.
    for (std::size_t i = 0; i < qcGamepadAxisCount; ++i) {
        state.setAxis(QcGamepadAxis(i), float(SDL_GetGamepadAxis(gamepad, sdlAxes[i])) / 32767.0f);
    }
    return state;
}

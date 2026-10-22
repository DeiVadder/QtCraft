#include "app/display/QcDisplaySettings.h"

#include "app/controllers/QcSettingsController.h"
#include "app/debug/QcAppLogging.h"
#include "clientcore/settings/QcKeyControls.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopedValueRollback>
#include <QScreen>
#include <QSettings>
#include <QSurfaceFormat>

using namespace Qt::StringLiterals;

namespace {

using Mode = QcDisplayOptions::WindowMode;
static_assert(int(QcDisplaySettings::WindowMode::Windowed) == int(Mode::Windowed)
                  && int(QcDisplaySettings::WindowMode::Borderless) == int(Mode::Borderless)
                  && int(QcDisplaySettings::WindowMode::Fullscreen) == int(Mode::Fullscreen),
              "QcDisplaySettings::WindowMode follows QcDisplayOptions::WindowMode");

// The key capture of the controls page takes every key, the fullscreen key too.
bool capturingKeys(const QQuickWindow &window)
{
    const QQuickItem *focus = window.activeFocusItem();
    return focus && focus->objectName() == u"keyCapture"_s;
}

// How long the window rests before its frame counts as the player's.
constexpr int noteDelayMs = 400;

} // namespace

QcDisplaySettings::QcDisplaySettings(const QcKeyControls &keys, QObject *parent)
    : QObject(parent)
    , m_keys(keys)
    , m_options(QcDisplayOptions::load(QcSettingsController::settingsPath()))
{
    connect(&m_placement, &QcWindowPlacement::fullscreenSwitched, this, &QcDisplaySettings::followFullscreen);
    if (auto *application = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        connect(application, &QGuiApplication::screenAdded, this, &QcDisplaySettings::screensChanged);
        connect(application, &QGuiApplication::screenRemoved, this, &QcDisplaySettings::onScreenRemoved);
        connect(application, &QCoreApplication::aboutToQuit, this, [this] {
            noteWindowedFrame();
            save();
        });
    }
    connect(this, &QcDisplaySettings::screensChanged, this, &QcDisplaySettings::changed);
    m_noteTimer.setSingleShot(true);
    m_noteTimer.setInterval(noteDelayMs);
    connect(&m_noteTimer, &QTimer::timeout, this, &QcDisplaySettings::noteWindowedFrame);
}

QcDisplaySettings::~QcDisplaySettings() = default;

void QcDisplaySettings::showWindow(QQuickWindow &window)
{
    // Read once when the window is made: the swap chain asks for no VSync with swap interval 0
    // (QRhiSwapChain::NoVSync: Metal's displaySync, the present flags of D3D, Vulkan's present mode).
    QSurfaceFormat format = window.requestedFormat();
    format.setSwapInterval(m_options.vsync ? 1 : 0);
    window.setFormat(format);
    m_vsyncInEffect = m_options.vsync;

    m_window = &window;
    window.installEventFilter(this);
    m_placement.setWindow(&window);
    place();
    // The windowed frame follows the window as the player moves and resizes it; stored at quit
    // (the window is hidden by then).
    connect(&window, &QWindow::xChanged, this, &QcDisplaySettings::onWindowMoved);
    connect(&window, &QWindow::yChanged, this, &QcDisplaySettings::onWindowMoved);
    connect(&window, &QWindow::widthChanged, this, &QcDisplaySettings::onWindowMoved);
    connect(&window, &QWindow::heightChanged, this, &QcDisplaySettings::onWindowMoved);
    updateLimit();
    emit controlsWindowChanged();
    emit changed();
}

int QcDisplaySettings::screen() const
{
    const QStringList names = QcWindowPlacement::screenNames();
    if (const std::optional<qsizetype> index = QcDisplayOptions::screenIndex(names, m_options.screen)) {
        return int(*index);
    }
    const QScreen *primary = QGuiApplication::primaryScreen();
    return primary ? std::max(0, int(names.indexOf(primary->name()))) : 0;
}

void QcDisplaySettings::setScreen(int index)
{
    const QStringList names = QcWindowPlacement::screenNames();
    if (index < 0 || index >= names.size()) {
        qCWarning(lcDisplay) << "Screen" << index << "rejected: there are" << names.size() << "screens";
        return;
    }
    if (names[index] == m_options.screen) {
        // The same screen: nothing to move.
        return;
    }
    m_options.screen = names[index];
    // A window moved to another screen starts centred there.
    m_options.windowGeometry = {};
    if (m_window) {
        place();
    }
    save();
    emit changed();
}

void QcDisplaySettings::setWindowMode(WindowMode mode)
{
    if (mode != WindowMode::Windowed && mode != WindowMode::Borderless && mode != WindowMode::Fullscreen) {
        qCWarning(lcDisplay) << "Window mode" << int(mode) << "rejected: unknown";
        return;
    }
    switchMode(Mode(mode));
}

void QcDisplaySettings::toggleFullscreen()
{
    switchMode(m_options.toggledFullscreen());
}

void QcDisplaySettings::switchMode(Mode mode)
{
    if (mode == m_options.windowMode) {
        // Unchanged: the window already shows it.
        return;
    }
    noteWindowedFrame();
    if (mode == Mode::Fullscreen) {
        m_options.previousMode = m_options.windowMode;
    }
    m_options.windowMode = mode;
    if (m_window) {
        place();
    }
    save();
    emit changed();
}

void QcDisplaySettings::followFullscreen(bool fullscreen)
{
    if (fullscreen) {
        // Already fullscreen: the setting follows without switching again. The frame noted last
        // stays (the window grew to the screen before the platform reported fullscreen).
        m_noteTimer.stop();
        m_options.previousMode = m_options.windowMode;
        m_options.windowMode = Mode::Fullscreen;
        save();
        emit changed();
        return;
    }
    // Left by the platform: back to the mode before, with its frame (a borderless window lost it).
    m_options.windowMode = Mode::Fullscreen;
    switchMode(m_options.toggledFullscreen());
}

void QcDisplaySettings::setVsync(bool on)
{
    if (on == m_options.vsync) {
        return;
    }
    m_options.vsync = on;
    save();
    emit changed();
}

std::optional<int> QcDisplaySettings::checkedLimit(FrameLimit limit, const char *setting)
{
    if (!QcDisplayOptions::isFrameLimit(int(limit))) {
        qCWarning(lcDisplay) << "Frame limit" << int(limit) << "for" << setting << "rejected: not one of the choices";
        return std::nullopt;
    }
    return int(limit);
}

void QcDisplaySettings::setFrameLimit(FrameLimit limit)
{
    const std::optional<int> checked = checkedLimit(limit, "frameLimit");
    if (!checked || *checked == m_options.frameLimit) {
        return;
    }
    m_options.frameLimit = *checked;
    save();
    updateLimit();
    emit changed();
}

void QcDisplaySettings::setBackgroundFrameLimit(FrameLimit limit)
{
    const std::optional<int> checked = checkedLimit(limit, "backgroundFrameLimit");
    if (!checked || *checked == m_options.backgroundFrameLimit) {
        return;
    }
    m_options.backgroundFrameLimit = *checked;
    save();
    updateLimit();
    emit changed();
}

void QcDisplaySettings::setForeground(bool foreground)
{
    if (foreground != m_foreground) {
        m_foreground = foreground;
        updateLimit();
    }
}

void QcDisplaySettings::updateLimit()
{
    // Scripted runs (no window handed over) draw every frame, as measurements and screenshots expect.
    const int limit = m_window ? m_options.activeFrameLimit(m_foreground) : 0;
    if (limit != m_frames.limit()) {
        m_frames.setLimit(limit);
        emit activeFrameLimitChanged();
    }
}

void QcDisplaySettings::place()
{
    const QScopedValueRollback placing(m_placing, true);
    m_placement.apply(m_options);
}

void QcDisplaySettings::onWindowMoved()
{
    if (m_placing || m_placement.settling()) {
        // The steps of a switch, not the player's frame.
        return;
    }
    m_noteTimer.start();
}

void QcDisplaySettings::onScreenRemoved(const QScreen *screen)
{
    emit screensChanged();
    if (!m_window || !screen || (screen != m_window->screen() && screen->name() != m_options.screen)) {
        return;
    }
    // Queued: the screen is gone from QGuiApplication::screens() then, and the placement falls
    // back to the primary screen (with a warning for the stored one).
    QMetaObject::invokeMethod(
        this,
        [this] {
            if (m_window) {
                place();
            }
        },
        Qt::QueuedConnection);
}

void QcDisplaySettings::noteWindowedFrame()
{
    m_noteTimer.stop();
    if (m_placing) {
        // apply() is switching: the window's frame now is none the player chose.
        return;
    }
    const QRect frame = m_placement.windowedFrame();
    if (!frame.isEmpty()) {
        m_options.windowGeometry = frame;
        m_options.screen = m_placement.screenName();
    }
}

void QcDisplaySettings::save() const
{
    QSettings file(QcSettingsController::settingsPath(), QSettings::IniFormat);
    m_options.save(file);
    file.sync();
    if (file.status() != QSettings::NoError) {
        qCWarning(lcDisplay).noquote() << u"Cannot write the display settings to %1"_s.arg(file.fileName());
    }
}

bool QcDisplaySettings::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_window && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<const QKeyEvent *>(event);
        if (!key->isAutoRepeat() && !capturingKeys(*m_window)
            && m_keys.bindings().actionForPress(key->key(), key->modifiers()) == QcKeyBindings::Action::Fullscreen) {
            toggleFullscreen();
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

#include "clientcore/settings/QcDisplayOptions.h"

#include "clientcore/debug/QcClientLogging.h"
#include "clientcore/settings/QcScreenChoice.h"

#include <QFileInfo>
#include <QSettings>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

constexpr std::array<QStringView, 3> modeNames{u"windowed", u"borderless", u"fullscreen"};

constexpr QLatin1StringView windowModeKey("display/windowMode");
constexpr QLatin1StringView previousModeKey("display/previousMode");
constexpr QLatin1StringView screenKey("display/screen");
constexpr QLatin1StringView geometryKey("display/windowGeometry");
constexpr QLatin1StringView vsyncKey("display/vsync");
constexpr QLatin1StringView frameLimitKey("display/frameLimit");
constexpr QLatin1StringView backgroundLimitKey("display/backgroundFrameLimit");

// A stored mode; a missing key gives `fallback` silently, an unknown name with a warning.
QcDisplayOptions::WindowMode loadMode(const QSettings &file, QLatin1StringView key, QcDisplayOptions::WindowMode fallback)
{
    const QVariant value = file.value(key);
    if (!value.isValid()) {
        return fallback;
    }
    if (const auto mode = QcDisplayOptions::modeFromName(value.toString())) {
        return *mode;
    }
    qCWarning(lcSettings).noquote() << u"Unknown window mode '%1' in %2; using %3"_s.arg(value.toString(), key,
                                                                                       QcDisplayOptions::modeName(fallback));
    return fallback;
}

// A stored frame limit; anything but one of the choices gives `fallback` (a warning if present).
int loadLimit(const QSettings &file, QLatin1StringView key, int fallback)
{
    const QVariant value = file.value(key);
    if (!value.isValid()) {
        return fallback;
    }
    bool ok = false;
    const int limit = value.toInt(&ok);
    if (ok && QcDisplayOptions::isFrameLimit(limit)) {
        return limit;
    }
    qCWarning(lcSettings).noquote() << u"Unsupported frame limit '%1' in %2; using %3"_s.arg(value.toString(), key).arg(fallback);
    return fallback;
}

} // namespace

QString QcDisplayOptions::modeName(WindowMode mode)
{
    return modeNames[std::size_t(mode)].toString();
}

std::optional<QcDisplayOptions::WindowMode> QcDisplayOptions::modeFromName(QStringView name)
{
    const auto found = std::ranges::find(modeNames, name);
    if (found == modeNames.end()) {
        return std::nullopt;
    }
    return WindowMode(found - modeNames.begin());
}

bool QcDisplayOptions::isFrameLimit(int framesPerSecond)
{
    return std::ranges::find(frameLimits, framesPerSecond) != frameLimits.end();
}

int QcDisplayOptions::activeFrameLimit(bool foreground) const
{
    if (foreground || backgroundFrameLimit == 0) {
        return frameLimit;
    }
    return frameLimit == 0 ? backgroundFrameLimit : std::min(frameLimit, backgroundFrameLimit);
}

QcDisplayOptions::WindowMode QcDisplayOptions::toggledFullscreen() const
{
    if (windowMode != WindowMode::Fullscreen) {
        return WindowMode::Fullscreen;
    }
    return previousMode == WindowMode::Fullscreen ? WindowMode::Windowed : previousMode;
}

std::optional<qsizetype> QcDisplayOptions::screenIndex(const QStringList &names, const QString &name)
{
    if (name.isEmpty()) {
        return std::nullopt;
    }
    const qsizetype index = names.indexOf(name);
    return index >= 0 ? std::optional(index) : std::nullopt;
}

QRect QcDisplayOptions::windowedFrame(QRect stored, QRect available, QSize defaultSize, QMargins frameMargins)
{
    // The area the client area may cover with the window's frame around it.
    const QRect inside = available.marginsRemoved(frameMargins);
    if (stored.isEmpty() || !available.contains(stored.center())) {
        return QcScreenChoice::placeInside(inside, defaultSize);
    }
    const QRect fitted(stored.topLeft(), stored.size().boundedTo(inside.size()));
    // Moved inside the screen where it sticks out.
    const int x = std::clamp(fitted.x(), inside.left(), inside.right() + 1 - fitted.width());
    const int y = std::clamp(fitted.y(), inside.top(), inside.bottom() + 1 - fitted.height());
    return {QPoint(x, y), fitted.size()};
}

QcDisplayOptions QcDisplayOptions::load(const QSettings &file)
{
    QcDisplayOptions options;
    options.windowMode = loadMode(file, windowModeKey, WindowMode::Windowed);
    options.previousMode = loadMode(file, previousModeKey, WindowMode::Windowed);
    if (options.previousMode == WindowMode::Fullscreen) {
        options.previousMode = WindowMode::Windowed;
    }
    options.screen = file.value(screenKey).toString();
    options.windowGeometry = file.value(geometryKey).toRect();
    options.vsync = file.value(vsyncKey, true).toBool();
    options.frameLimit = loadLimit(file, frameLimitKey, defaultFrameLimit);
    options.backgroundFrameLimit = loadLimit(file, backgroundLimitKey, defaultBackgroundFrameLimit);
    return options;
}

QcDisplayOptions QcDisplayOptions::load(const QString &path)
{
    if (!QFileInfo(path).isFile()) {
        return {};
    }
    const QSettings file(path, QSettings::IniFormat);
    return load(file);
}

void QcDisplayOptions::save(QSettings &file) const
{
    file.setValue(windowModeKey, modeName(windowMode));
    file.setValue(previousModeKey, modeName(previousMode));
    file.setValue(screenKey, screen);
    file.setValue(geometryKey, windowGeometry);
    file.setValue(vsyncKey, vsync);
    file.setValue(frameLimitKey, frameLimit);
    file.setValue(backgroundLimitKey, backgroundFrameLimit);
}

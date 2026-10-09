#include "QcSettingsController.h"

#include "QcClientSession.h"
#include "QcSessionParts.h"
#include "QcWorldLibrary.h"

#include <QDir>
#include <QLoggingCategory>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcGame, "qtcraft.game")

} // namespace

QcSettingsController::QcSettingsController(const QcSessionParts &parts, const QcWorldLibraryController &library,
                                           QcAudioEngine *audio, QObject *parent)
    : QObject(parent)
    , m_parts(parts)
    , m_keyControls(new QcKeyControls(this))
    , m_audio(new QcAudioSettings(audio, this))
    , m_character(new QcCharacterSettings(parts, library, *this, this))
{
    connect(m_keyControls, &QcKeyControls::edited, this, &QcSettingsController::saveSettings);
}

QcSettingsController::~QcSettingsController() = default;

QString QcSettingsController::settingsPath()
{
    return QDir(QcWorldLibrary::dataRoot()).filePath(u"settings.ini"_s);
}

QString QcSettingsController::storedLanguage()
{
    return QcGameSettings::load(settingsPath()).language;
}

void QcSettingsController::loadSettings()
{
    m_storedSettings = QcGameSettings::load(settingsPath());
    setViewDistance(m_storedSettings.viewDistance);
    setFieldOfView(m_storedSettings.fieldOfView);
    setMouseSensitivity(m_storedSettings.mouseSensitivity);
    setInvertMouse(m_storedSettings.invertMouse);
    setShowDebug(m_storedSettings.showDebug);
    m_languageSetting = m_storedSettings.language;
    m_cameraMode = m_storedSettings.cameraMode;
    m_character->load(m_storedSettings.appearance);
    m_keyControls->setBindings(m_storedSettings.keys);
    m_audio->load(m_storedSettings);
    emit settingsChanged();
}

void QcSettingsController::overrideViewDistance(int chunks)
{
    setViewDistance(chunks);
    m_commandLineViewDistance = m_viewDistance;
}

void QcSettingsController::saveSettings()
{
    QcGameSettings settings;
    // A view distance given on the command line is not stored unless the player changed it since.
    settings.viewDistance = m_commandLineViewDistance == m_viewDistance ? m_storedSettings.viewDistance : m_viewDistance;
    settings.fieldOfView = m_fieldOfView;
    settings.mouseSensitivity = m_mouseSensitivity;
    settings.invertMouse = m_invertMouse;
    settings.showDebug = m_showDebug;
    settings.language = m_languageSetting;
    settings.cameraMode = m_cameraMode;
    settings.appearance = m_character->appearance();
    m_audio->store(settings);
    settings.keys = m_keyControls->bindings();
    if (const QcStatus saved = settings.save(settingsPath()); !saved) {
        qCWarning(lcGame) << saved.error();
        return;
    }

    m_storedSettings = settings;
}

void QcSettingsController::setViewDistance(int chunks)
{
    chunks = QcGameSettings::clampViewDistance(chunks);
    if (m_viewDistance == chunks) {
        return;
    }

    m_viewDistance = chunks;
    emit viewDistanceChanged();
}

void QcSettingsController::setFieldOfView(double degrees)
{
    degrees = QcGameSettings::clampFieldOfView(degrees);
    if (m_fieldOfView != degrees) {
        m_fieldOfView = degrees;
        emit settingsChanged();
    }
}

void QcSettingsController::setMouseSensitivity(double sensitivity)
{
    sensitivity = QcGameSettings::clampMouseSensitivity(sensitivity);
    if (m_mouseSensitivity != sensitivity) {
        m_mouseSensitivity = sensitivity;
        emit settingsChanged();
    }
}

void QcSettingsController::setInvertMouse(bool invert)
{
    if (m_invertMouse != invert) {
        m_invertMouse = invert;
        emit settingsChanged();
    }
}

void QcSettingsController::setShowDebug(bool show)
{
    if (m_showDebug != show) {
        m_showDebug = show;
        emit debugOverlaySwitched();
        emit settingsChanged();
    }
}

void QcSettingsController::requestLanguage(const QString &requested)
{
    const QString language = QcGameSettings::cleanLanguage(requested);
    if (m_languageSetting == language) {
        return;
    }

    m_languageSetting = language;
    emit settingsChanged();
    emit languageChangeRequested(language);
}

QString QcSettingsController::cameraMode() const
{
    return QString::fromLatin1(QcViewCamera::name(m_cameraMode));
}

void QcSettingsController::setCameraMode(const QString &mode)
{
    const std::optional<QcCameraMode> parsed = QcViewCamera::fromName(mode);
    if (!parsed || *parsed == m_cameraMode) {
        return;
    }

    m_cameraMode = *parsed;
    if (m_parts.session) {
        m_parts.session->setCameraMode(m_cameraMode);
    }
    emit settingsChanged();
}

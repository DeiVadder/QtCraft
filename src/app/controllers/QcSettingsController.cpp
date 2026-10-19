#include "app/controllers/QcSettingsController.h"

#include "app/controllers/QcWorldLibraryController.h"
#include "app/debug/QcAppLogging.h"
#include "app/session/QcSessionParts.h"
#include "clientcore/session/QcClientSession.h"
#include "worlds/QcWorldLibrary.h"

#include <QDir>

using namespace Qt::StringLiterals;

namespace {

using CameraMode = QcSettingsController::CameraMode;
static_assert(int(CameraMode::FirstPerson) == int(QcCameraMode::FirstPerson) && int(CameraMode::Behind) == int(QcCameraMode::Behind)
                  && int(CameraMode::Front) == int(QcCameraMode::Front) && int(CameraMode::Side) == int(QcCameraMode::Side),
              "QcSettingsController::CameraMode follows QcCameraMode");

// A value from QML or the command line outside the setting's range is used clamped, with a
// warning naming both; values inside it pass silently.
void warnIfClamped(const char *setting, double requested, double used)
{
    if (requested != used) {
        qCWarning(lcGame) << "Setting" << setting << ": using" << used << "instead of" << requested << "(out of range)";
    }
}

} // namespace

QcSettingsController::QcSettingsController(const QcSessionParts &parts, const QcWorldLibraryController &library,
                                           QcAudioEngine *audio, QObject *parent)
    : QObject(parent)
    , m_parts(parts)
    , m_keyControls(new QcKeyControls(this))
    , m_audio(new QcAudioSettings(audio, this))
    , m_character(new QcCharacterSettings(parts, library, *this, this))
    , m_debug(new QcDebugSettings(this))
    , m_texturePacks(new QcTexturePackController([&library] { return library.modsDirectory(); }, this))
    , m_gamepad(new QcGamepadOptions(this))
{
    // The game controller follows settingsChanged (the pad's profiles apply at once).
    connect(m_gamepad, &QcGamepadOptions::changed, this, &QcSettingsController::settingsChanged);
    connect(m_keyControls, &QcKeyControls::edited, this, &QcSettingsController::saveSettings);
    connect(m_texturePacks, &QcTexturePackController::selectionChanged, this, &QcSettingsController::saveSettings);
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
    setFovEffects(m_storedSettings.fovEffects);
    setMouseSensitivity(m_storedSettings.mouseSensitivity);
    setInvertMouse(m_storedSettings.invertMouse);
    m_gamepad->load(m_storedSettings.gamepad);
    m_debug->load(m_storedSettings.debug);
    setShowLabels(m_storedSettings.showLabels);
    setPhotoHidesHand(m_storedSettings.photoHidesHand);
    m_languageSetting = m_storedSettings.language;
    m_cameraMode = m_storedSettings.cameraMode;
    m_character->load(m_storedSettings.appearance, m_storedSettings.characterPresets);
    m_keyControls->setBindings(m_storedSettings.keys);
    m_audio->load(m_storedSettings.audio);
    m_texturePacks->loadSelection(m_storedSettings.texturePacks);
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
    settings.fovEffects = m_fovEffects;
    settings.mouseSensitivity = m_mouseSensitivity;
    settings.invertMouse = m_invertMouse;
    settings.gamepad = m_gamepad->settings();
    settings.debug = m_debug->options();
    settings.showLabels = m_showLabels;
    settings.photoHidesHand = m_photoHidesHand;
    settings.language = m_languageSetting;
    settings.cameraMode = m_cameraMode;
    settings.appearance = m_character->appearance();
    settings.characterPresets = m_character->savedPresets();
    settings.audio = m_audio->volumes();
    settings.keys = m_keyControls->bindings();
    settings.texturePacks = m_texturePacks->storedSelection();
    if (const QcStatus saved = settings.save(settingsPath()); !saved) {
        qCWarning(lcGame) << saved.error();
        return;
    }

    m_storedSettings = settings;
}

void QcSettingsController::setViewDistance(int chunks)
{
    const auto requested = chunks;
    chunks = QcGameSettings::clampViewDistance(chunks);
    warnIfClamped("viewDistance", requested, chunks);
    if (m_viewDistance == chunks) {
        return;
    }

    m_viewDistance = chunks;
    emit viewDistanceChanged();
}

void QcSettingsController::setFieldOfView(double degrees)
{
    const auto requested = degrees;
    degrees = QcGameSettings::clampFieldOfView(degrees);
    warnIfClamped("fieldOfView", requested, degrees);
    if (m_fieldOfView != degrees) {
        m_fieldOfView = degrees;
        emit settingsChanged();
    }
}

void QcSettingsController::setFovEffects(double share)
{
    const auto requested = share;
    share = QcGameSettings::clampFovEffects(share);
    warnIfClamped("fovEffects", requested, share);
    if (m_fovEffects != share) {
        m_fovEffects = share;
        emit settingsChanged();
    }
}

void QcSettingsController::setMouseSensitivity(double sensitivity)
{
    const auto requested = sensitivity;
    sensitivity = QcGameSettings::clampMouseSensitivity(sensitivity);
    warnIfClamped("mouseSensitivity", requested, sensitivity);
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

void QcSettingsController::setShowLabels(bool show)
{
    if (m_showLabels != show) {
        m_showLabels = show;
        emit settingsChanged();
    }
}

void QcSettingsController::setPhotoHidesHand(bool hides)
{
    if (m_photoHidesHand != hides) {
        m_photoHidesHand = hides;
        emit settingsChanged();
    }
}

void QcSettingsController::requestLanguage(const QString &requested)
{
    const QString language = QcGameSettings::cleanLanguage(requested);
    if (language != requested) {
        qCWarning(lcGame) << "Setting language: using" << language << "instead of" << requested << "(not a locale name)";
    }
    if (m_languageSetting == language) {
        return;
    }

    m_languageSetting = language;
    emit settingsChanged();
    emit languageChangeRequested(language);
}

void QcSettingsController::setCameraMode(CameraMode mode)
{
    if (mode < CameraMode::FirstPerson || mode > CameraMode::Side) {
        qCWarning(lcGame) << "Ignoring unknown camera mode" << int(mode);
        return;
    }
    setCameraModeValue(QcCameraMode(mode));
}

void QcSettingsController::setCameraModeValue(QcCameraMode mode)
{
    // The same mode again changes nothing (the screen's box sets it on every choice).
    if (mode == m_cameraMode) {
        return;
    }

    m_cameraMode = mode;
    if (m_parts.session) {
        m_parts.session->setCameraMode(m_cameraMode);
    }
    emit settingsChanged();
}

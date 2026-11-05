// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/settings/QcSettingsController.h"

#include "app/controllers/QcWorldLibraryController.h"
#include "app/debug/QcAppLogging.h"
#include "app/session/QcSessionParts.h"
#include "clientcore/session/QcClientSession.h"
#include "worlds/QcWorldLibrary.h"

#include <QCoreApplication>
#include <QDir>

using namespace Qt::StringLiterals;

namespace {

using CameraMode = QcSettingsController::CameraMode;
static_assert(static_cast<int>(CameraMode::FirstPerson) == static_cast<int>(QcCameraMode::FirstPerson)
                  && static_cast<int>(CameraMode::Behind) == static_cast<int>(QcCameraMode::Behind)
                  && static_cast<int>(CameraMode::Front) == static_cast<int>(QcCameraMode::Front)
                  && static_cast<int>(CameraMode::Side) == static_cast<int>(QcCameraMode::Side),
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
    , m_display(new QcDisplaySettings(*m_keyControls, this))
    , m_diagnostics(new QcDiagnosticsSettings(this))
    , m_character(new QcCharacterSettings(parts, library, *this, this))
    , m_debug(new QcDebugSettings(this))
    , m_texturePacks(new QcTexturePackController([&library] { return library.modsDirectory(); }, this))
    , m_gamepad(new QcGamepadOptions(this))
    , m_rendering(new QcRenderSettings(this))
{
    // The game controller follows settingsChanged (the pad's profiles apply at once).
    connect(m_gamepad, &QcGamepadOptions::changed, this, &QcSettingsController::settingsChanged);
    connect(m_rendering, &QcRenderSettings::changed, this, &QcSettingsController::settingsChanged);
    // The atlas carries material maps only while the realistic mode draws them (decision E-908).
    connect(m_rendering, &QcRenderSettings::changed, this, [this] { m_texturePacks->setMaterials(m_rendering->options().materials().maps); });
    connect(m_keyControls, &QcKeyControls::edited, this, &QcSettingsController::saveSettings);
    connect(m_texturePacks, &QcTexturePackController::selectionChanged, this, &QcSettingsController::saveSettings);

    // Every other change is stored once the values rest (saveIfChanged() skips what is stored already).
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(saveDelayMs);
    connect(&m_saveTimer, &QTimer::timeout, this, &QcSettingsController::saveIfChanged);
    connect(this, &QcSettingsController::settingsChanged, this, &QcSettingsController::scheduleSave);
    connect(this, &QcSettingsController::viewDistanceChanged, this, &QcSettingsController::scheduleSave);
    connect(m_audio, &QcAudioSettings::changed, this, &QcSettingsController::scheduleSave);
    // A change of the debug display releases what the scene held of it (QcSceneOverrides).
    connect(m_debug, &QcDebugSettings::levelChanged, this, [this] {
        m_sceneOverrides.releaseDebugLevel();
        scheduleSave();
    });
    connect(m_debug, &QcDebugSettings::visualsChanged, this, [this] {
        m_sceneOverrides.releaseChangedVisuals(m_debug->options());
        scheduleSave();
    });
    connect(m_character, &QcCharacterSettings::lookChanged, this, &QcSettingsController::scheduleSave);
    connect(m_character, &QcCharacterSettings::presetsChanged, this, &QcSettingsController::scheduleSave);
    connect(m_gamepad, &QcGamepadOptions::controllersChanged, this, &QcSettingsController::scheduleSave);
    if (const QCoreApplication *application = QCoreApplication::instance()) {
        connect(application, &QCoreApplication::aboutToQuit, this, &QcSettingsController::flushPendingSave);
    }
}

QcSettingsController::~QcSettingsController()
{
    flushPendingSave();
}

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
    setPauseOnFocusLoss(m_storedSettings.pauseOnFocusLoss);
    setCameraShake(m_storedSettings.cameraShake);
    m_languageSetting = m_storedSettings.language;
    m_cameraMode = m_storedSettings.cameraMode;
    // Auto (older settings files) means 100 %, the step the screen offers for it.
    m_uiScale = m_storedSettings.uiScale == QcUiScale::Step::Auto ? QcUiScale::Step::Percent100 : m_storedSettings.uiScale;
    if (m_storedSettings.uiScale == QcUiScale::Step::Auto) {
        qCInfo(lcGame) << "Settings: UI scale auto is 100 %";
    }
    m_textureFiltering = static_cast<TextureFiltering>(m_storedSettings.textureFiltering);
    m_rendering->load(m_storedSettings.render);
    m_character->load(m_storedSettings.appearance, m_storedSettings.characterPresets);
    m_keyControls->setBindings(m_storedSettings.keys);
    m_audio->load(m_storedSettings.audio);
    m_texturePacks->loadSelection(m_storedSettings.texturePacks);
    emit settingsChanged();
    // What was just loaded is stored already.
    m_saveTimer.stop();
}

void QcSettingsController::overrideViewDistance(int chunks)
{
    setViewDistance(chunks);
    m_sceneOverrides.holdViewDistance();
}

QcGameSettings QcSettingsController::currentSettings() const
{
    QcGameSettings settings;
    settings.viewDistance = m_viewDistance;
    settings.fieldOfView = m_fieldOfView;
    settings.fovEffects = m_fovEffects;
    settings.mouseSensitivity = m_mouseSensitivity;
    settings.invertMouse = m_invertMouse;
    settings.gamepad = m_gamepad->settings();
    settings.debug = m_debug->options();
    settings.showLabels = m_showLabels;
    settings.photoHidesHand = m_photoHidesHand;
    settings.pauseOnFocusLoss = m_pauseOnFocusLoss;
    settings.cameraShake = m_cameraShake;
    settings.language = m_languageSetting;
    settings.cameraMode = m_cameraMode;
    settings.uiScale = m_uiScale;
    settings.textureFiltering = static_cast<int>(m_textureFiltering);
    settings.render = m_rendering->options();
    settings.appearance = m_character->appearance();
    settings.characterPresets = m_character->savedPresets();
    settings.audio = m_audio->volumes();
    settings.keys = m_keyControls->bindings();
    settings.texturePacks = m_texturePacks->storedSelection();
    // The run's scene options stand for the stored values until the player changes them.
    return m_sceneOverrides.toStore(settings, m_storedSettings);
}

void QcSettingsController::keepSceneOptionsUnsaved()
{
    m_saveTimer.stop();
    if (m_cameraMode != m_storedSettings.cameraMode) {
        m_sceneOverrides.holdCameraMode();
    }
    m_sceneOverrides.holdDebug(m_debug->options(), m_storedSettings.debug);
}

void QcSettingsController::saveSettings()
{
    m_saveTimer.stop();
    const QcGameSettings settings = currentSettings();
    if (const QcStatus saved = settings.save(settingsPath()); !saved) {
        qCWarning(lcGame) << saved.error();
        return;
    }

    m_storedSettings = settings;
}

void QcSettingsController::saveIfChanged()
{
    if (currentSettings() != m_storedSettings) {
        saveSettings();
    }
}

void QcSettingsController::flushPendingSave()
{
    if (savePending()) {
        m_saveTimer.stop();
        saveIfChanged();
    }
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
    m_sceneOverrides.releaseViewDistance();
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

void QcSettingsController::setPauseOnFocusLoss(bool pause)
{
    if (m_pauseOnFocusLoss != pause) {
        m_pauseOnFocusLoss = pause;
        emit settingsChanged();
    }
}

void QcSettingsController::setCameraShake(bool shake)
{
    if (m_cameraShake != shake) {
        m_cameraShake = shake;
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
        qCWarning(lcGame) << "Ignoring unknown camera mode" << static_cast<int>(mode);
        return;
    }
    setCameraModeValue(static_cast<QcCameraMode>(mode));
}

void QcSettingsController::setCameraModeValue(QcCameraMode mode)
{
    // The same mode again changes nothing (the screen's box sets it on every choice).
    if (mode == m_cameraMode) {
        return;
    }

    m_cameraMode = mode;
    m_sceneOverrides.releaseCameraMode();
    if (m_parts.session) {
        m_parts.session->setCameraMode(m_cameraMode);
    }
    emit settingsChanged();
}

void QcSettingsController::setUiScale(QcUiScaling::Step step)
{
    if (!QcUiScaling::known(step)) {
        qCWarning(lcGame) << "Ignoring unknown UI scale" << static_cast<int>(step);
        return;
    }
    // The same step again changes nothing (the screen's box sets it on every choice).
    if (QcUiScaling::toValue(step) == m_uiScale) {
        return;
    }

    m_uiScale = QcUiScaling::toValue(step);
    emit settingsChanged();
}

void QcSettingsController::setTextureFiltering(TextureFiltering filtering)
{
    if (!QcGameSettings::isTextureFiltering(static_cast<int>(filtering))) {
        qCWarning(lcGame) << "Ignoring unknown texture filtering" << static_cast<int>(filtering);
        return;
    }
    // The same choice again changes nothing (the screen's box sets it on every choice).
    if (filtering == m_textureFiltering) {
        return;
    }

    m_textureFiltering = filtering;
    emit settingsChanged();
}

QcSettingsController::KeyboardLayout QcSettingsController::keyboardLayoutOf(const QString &language)
{
    return language == u"de"_s ? KeyboardLayout::Qwertz : KeyboardLayout::Qwerty;
}

void QcSettingsController::setContentLanguage(const QString &language)
{
    // The same language again (a switch to the system language that is already shown) stays silent.
    if (m_contentLanguage == language) {
        return;
    }
    m_contentLanguage = language;
    emit contentLanguageChanged();
}

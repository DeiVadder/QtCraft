#pragma once

#include "app/controllers/QcAudioSettings.h"
#include "app/controllers/QcCharacterSettings.h"
#include "app/controllers/QcGamepadOptions.h"
#include "app/controllers/QcTexturePackController.h"
#include "app/debug/QcDebugSettings.h"
#include "app/input/QcKeyControlsForeign.h"
#include "app/models/QcUiScaling.h"
#include "clientcore/camera/QcCamera.h"
#include "clientcore/settings/QcGameSettings.h"

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

class QcAudioEngine;
class QcWorldLibraryController;
struct QcSessionParts;

// The player's settings (GameController.settings), stored in a file below the data root
// (QTCRAFT_DATA_DIR in tests): view distance, field of view and its effects, mouse, labels, language, UI scale, camera mode
// and keys, with the volumes, the character's look and the debug display as their own objects. The accessors
// are the QML properties of this one responsibility.
class QcSettingsController : public QObject
{
    Q_OBJECT
    // GameSettings in QML, for its enumerations (GameSettings.CameraMode.Behind).
    QML_NAMED_ELEMENT(GameSettings)
    QML_UNCREATABLE("Provided by GameController.settings")
    Q_PROPERTY(int viewDistance READ viewDistance WRITE setViewDistance NOTIFY viewDistanceChanged FINAL)
    Q_PROPERTY(double fieldOfView READ fieldOfView WRITE setFieldOfView NOTIFY settingsChanged FINAL)
    // Share (0..1) of the field-of-view widening while sprinting (decision E-280).
    Q_PROPERTY(double fovEffects READ fovEffects WRITE setFovEffects NOTIFY settingsChanged FINAL)
    Q_PROPERTY(double mouseSensitivity READ mouseSensitivity WRITE setMouseSensitivity NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool invertMouse READ invertMouse WRITE setInvertMouse NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool showLabels READ showLabels WRITE setShowLabels NOTIFY settingsChanged FINAL)
    // Photo mode: hiding the HUD hides the hand as well (decision E-692).
    Q_PROPERTY(bool photoHidesHand READ photoHidesHand WRITE setPhotoHidesHand NOTIFY settingsChanged FINAL)
    // "" follows the system language; otherwise a locale name such as "de".
    Q_PROPERTY(QString language READ language WRITE requestLanguage NOTIFY settingsChanged FINAL)
    // GameSettings.CameraMode.FirstPerson, Behind, Front or Side.
    Q_PROPERTY(QcSettingsController::CameraMode cameraMode READ cameraMode WRITE setCameraMode NOTIFY settingsChanged FINAL)
    // Size of the HUD and the screens (decision E-725): UiScaling.Step.Auto or Percent75 … Percent200.
    Q_PROPERTY(QcUiScaling::Step uiScale READ uiScale WRITE setUiScale NOTIFY settingsChanged FINAL)
    // The player's keys.
    Q_PROPERTY(QcKeyControls *keys READ keys CONSTANT FINAL)
    Q_PROPERTY(QcAudioSettings *audio READ audio CONSTANT FINAL)
    Q_PROPERTY(QcCharacterSettings *character READ character CONSTANT FINAL)
    // Debug display level and visualisations (decision E-225).
    Q_PROPERTY(QcDebugSettings *debug READ debug CONSTANT FINAL)
    // Texture packs and their order (decision E-297).
    Q_PROPERTY(QcTexturePackController *texturePacks READ texturePacks CONSTANT FINAL)
    // The gamepad page: profiles, buttons, the radial menu alone (decisions E-610, E-611, E-614).
    Q_PROPERTY(QcGamepadOptions *gamepad READ gamepadOptions CONSTANT FINAL)

public:
    // QcCameraMode for QML, in its order.
    enum class CameraMode { FirstPerson, Behind, Front, Side };
    Q_ENUM(CameraMode)

    QcSettingsController(const QcSessionParts &parts, const QcWorldLibraryController &library, QcAudioEngine *audio,
                         QObject *parent = nullptr);
    ~QcSettingsController() override;

    static QString settingsPath();
    // Language stored in the settings ("" = system).
    static QString storedLanguage();
    void loadSettings();
    Q_INVOKABLE void saveSettings();
    // View distance from the command line: used now, but not written to the settings.
    void overrideViewDistance(int chunks);

    int viewDistance() const { return m_viewDistance; }
    void setViewDistance(int chunks);
    double fieldOfView() const { return m_fieldOfView; }
    void setFieldOfView(double degrees);
    double fovEffects() const { return m_fovEffects; }
    void setFovEffects(double share);
    double mouseSensitivity() const { return m_mouseSensitivity; }
    void setMouseSensitivity(double sensitivity);
    bool invertMouse() const { return m_invertMouse; }
    void setInvertMouse(bool invert);
    // Profiles, buttons and the radial menu alone of the gamepads (decisions E-541, E-610, E-611).
    const QcGamepadSettings &gamepad() const { return m_gamepad->settings(); }
    QcGamepadOptions *gamepadOptions() const { return m_gamepad; }
    // Labels floating in the world (decision E-222); the label key switches them.
    bool showLabels() const { return m_showLabels; }
    void setShowLabels(bool show);
    bool photoHidesHand() const { return m_photoHidesHand; }
    void setPhotoHidesHand(bool hides);
    QString language() const { return m_languageSetting; }
    // Stores the choice and asks the application to switch the UI language now.
    void requestLanguage(const QString &requested);
    // First person, behind, front or side; the camera view key cycles through them.
    CameraMode cameraMode() const { return CameraMode(m_cameraMode); }
    void setCameraMode(CameraMode mode);
    QcCameraMode cameraModeValue() const { return m_cameraMode; }
    void setCameraModeValue(QcCameraMode mode);
    QcUiScaling::Step uiScale() const { return QcUiScaling::fromValue(m_uiScale); }
    void setUiScale(QcUiScaling::Step step);
    QcKeyControls *keys() const { return m_keyControls; }
    QcAudioSettings *audio() const { return m_audio; }
    QcCharacterSettings *character() const { return m_character; }
    QcDebugSettings *debug() const { return m_debug; }
    QcTexturePackController *texturePacks() const { return m_texturePacks; }

    // Language used for content names (block and item labels).
    QString contentLanguage() const { return m_contentLanguage; }
    void setContentLanguage(const QString &language) { m_contentLanguage = language; }

signals:
    void settingsChanged();
    void viewDistanceChanged();
    // The application installs the translator for `language` ("" = system) and retranslates.
    void languageChangeRequested(const QString &language);

private:
    const QcSessionParts &m_parts;
    QcKeyControls *m_keyControls = nullptr;
    QcAudioSettings *m_audio = nullptr;
    QcCharacterSettings *m_character = nullptr;
    QcDebugSettings *m_debug = nullptr;
    QcTexturePackController *m_texturePacks = nullptr;
    QcGamepadOptions *m_gamepad = nullptr;
    int m_viewDistance = QcGameSettings::defaultViewDistance;
    double m_fieldOfView = QcCamera::defaultFieldOfView;
    double m_fovEffects = QcGameSettings::defaultFovEffects;
    double m_mouseSensitivity = QcGameSettings::defaultMouseSensitivity;
    bool m_invertMouse = false;
    bool m_showLabels = true;
    bool m_photoHidesHand = true;
    QString m_languageSetting;
    QcCameraMode m_cameraMode = QcCameraMode::FirstPerson;
    QcUiScale::Step m_uiScale = QcUiScale::defaultStep;
    QString m_contentLanguage = QStringLiteral("en");
    QcGameSettings m_storedSettings;
    int m_commandLineViewDistance = 0;
};

#include "clientcore/settings/QcGameSettings.h"

#include "clientcore/debug/QcClientLogging.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

QString keySetting(QcKeyBindings::Action action, int slot)
{
    return u"keys/"_s + QcKeyBindings::id(action) + (slot == 0 ? QString() : u".%1"_s.arg(slot + 1));
}

// Active texture packs as an INI array ("texturePacks/<n>/id"), so ids may contain commas.
QStringList loadTexturePacks(QSettings &file)
{
    QStringList ids;
    const int count = std::min(file.beginReadArray(u"texturePacks"_s), 4 * QcGameSettings::maxTexturePacks);
    for (int i = 0; i < count; ++i) {
        file.setArrayIndex(i);
        ids.append(file.value(u"id"_s).toString());
    }
    file.endArray();
    return QcGameSettings::cleanTexturePacks(ids);
}

void saveTexturePacks(QSettings &file, const QStringList &ids)
{
    file.remove(u"texturePacks"_s);
    file.beginWriteArray(u"texturePacks"_s, static_cast<int>(ids.size()));
    for (int i = 0; i < ids.size(); ++i) {
        file.setArrayIndex(i);
        file.setValue(u"id"_s, ids[i]);
    }
    file.endArray();
}

QcKeyBindings loadKeys(const QSettings &file)
{
    // setKey() ignores keys the platform cannot bind; those slots keep their default.
    QcKeyBindings keys = QcKeyBindings::defaults();
    QList<std::pair<QcKeyBindings::Action, int>> fromFile;
    for (const QcKeyBindings::Action action : QcKeyBindings::actions()) {
        if (QcKeyBindings::isFixed(action)) {
            continue;
        }

        for (int slot = 0; slot < QcKeyBindings::slotCount; ++slot) {
            const QVariant value = file.value(keySetting(action, slot));
            const std::optional<int> key = value.isValid() ? QcKeyBindings::keyFromName(value.toString()) : std::nullopt;
            if (key) {
                keys.setKey(action, slot, *key);
            }
            if (key && keys.key(action, slot) == *key) {
                fromFile.append({action, slot});
            }
        }
    }

    // A key the player chose beats a default that a newer version gave to another action; that
    // action goes without it (its other key, if any, still works).
    const QcKeyBindings before = keys;
    const QList<std::pair<QcKeyBindings::Action, int>> dropped = keys.dropConflicts(fromFile);
    for (const auto &[action, slot] : dropped) {
        qCWarning(lcSettings).noquote() << u"Key %1 of '%2' is already bound to another action; '%2' goes without it"_s.arg(
            QcKeyBindings::keyName(before.key(action, slot)), QcKeyBindings::id(action));
    }
    return keys;
}

} // namespace

int QcGameSettings::clampViewDistance(int chunks)
{
    return std::clamp(chunks, minViewDistance, maxViewDistance);
}

double QcGameSettings::clampFieldOfView(double degrees)
{
    return std::isfinite(degrees) ? std::clamp(degrees, minFieldOfView, maxFieldOfView) : defaultFieldOfView;
}

double QcGameSettings::clampFovEffects(double share)
{
    return std::isfinite(share) ? std::clamp(share, 0.0, 1.0) : defaultFovEffects;
}

double QcGameSettings::clampMouseSensitivity(double sensitivity)
{
    return std::isfinite(sensitivity) ? std::clamp(sensitivity, minMouseSensitivity, maxMouseSensitivity)
                                      : defaultMouseSensitivity;
}

double QcGameSettings::clampVolume(double volume, double fallback)
{
    return std::isfinite(volume) ? std::clamp(volume, 0.0, 1.0) : fallback;
}

QString QcGameSettings::cleanLanguage(const QString &language)
{
    static const QRegularExpression code(u"^[a-z]{2}$"_s);
    return code.match(language).hasMatch() ? language : QString();
}

QStringList QcGameSettings::cleanTexturePacks(const QStringList &ids)
{
    QStringList clean;
    for (const QString &id : ids) {
        const QString trimmed = id.trimmed();
        if (!trimmed.isEmpty() && !clean.contains(trimmed) && clean.size() < maxTexturePacks) {
            clean.append(trimmed);
        }
    }
    return clean;
}

QcGameSettings QcGameSettings::load(const QString &path)
{
    QcGameSettings settings;
    if (!QFileInfo(path).isFile()) {
        return settings;
    }

    QSettings file(path, QSettings::IniFormat);
    bool ok = false;
    const int view = file.value(u"viewDistance"_s).toInt(&ok);
    settings.viewDistance = ok ? clampViewDistance(view) : defaultViewDistance;
    const double fov = file.value(u"fieldOfView"_s).toDouble(&ok);
    settings.fieldOfView = ok ? clampFieldOfView(fov) : defaultFieldOfView;
    const double fovEffects = file.value(u"fovEffects"_s).toDouble(&ok);
    settings.fovEffects = ok ? clampFovEffects(fovEffects) : defaultFovEffects;
    const double sensitivity = file.value(u"mouseSensitivity"_s).toDouble(&ok);
    settings.mouseSensitivity = ok ? clampMouseSensitivity(sensitivity) : defaultMouseSensitivity;
    settings.invertMouse = file.value(u"invertMouse"_s, false).toBool();
    settings.debug = QcDebugOptions::load(file);
    settings.showLabels = file.value(u"showLabels"_s, true).toBool();
    settings.photoHidesHand = file.value(u"photoHidesHand"_s, true).toBool();
    settings.pauseOnFocusLoss = file.value(u"pauseOnFocusLoss"_s, false).toBool();
    settings.language = cleanLanguage(file.value(u"language"_s).toString());
    settings.cameraMode = QcViewCamera::fromName(file.value(u"cameraMode"_s).toString()).value_or(QcCameraMode::FirstPerson);
    settings.uiScale = QcUiScale::fromName(file.value(u"uiScale"_s).toString()).value_or(QcUiScale::defaultStep);
    const int filtering = file.value(u"textureFiltering"_s).toInt(&ok);
    settings.textureFiltering = ok && isTextureFiltering(filtering) ? filtering : defaultTextureFiltering;

    settings.appearance = QcCharacterProfile::loadAppearance(file, u"character"_s);
    settings.characterPresets = QcCharacterProfile::loadPresets(file);

    const auto volume = [&file](const QString &key, double fallback) {
        bool ok = false;
        const double value = file.value(key).toDouble(&ok);
        return ok ? clampVolume(value, fallback) : fallback;
    };

    settings.audio.master = volume(u"audio/master"_s, defaultMasterVolume);
    settings.audio.effects = volume(u"audio/effects"_s, defaultEffectsVolume);
    settings.audio.music = volume(u"audio/music"_s, defaultMusicVolume);
    settings.audio.ambient = volume(u"audio/ambient"_s, defaultAmbientVolume);
    settings.audio.muted = file.value(u"audio/muted"_s, false).toBool();
    settings.keys = loadKeys(file);
    settings.gamepad = QcGamepadSettings::load(file);
    settings.texturePacks = loadTexturePacks(file);
    return settings;
}

QcStatus QcGameSettings::save(const QString &path) const
{
    if (!QDir().mkpath(QFileInfo(path).path())) {
        return qcError(u"Cannot create %1"_s.arg(QFileInfo(path).path()));
    }

    QSettings file(path, QSettings::IniFormat);
    file.setValue(u"viewDistance"_s, viewDistance);
    file.setValue(u"fieldOfView"_s, fieldOfView);
    file.setValue(u"fovEffects"_s, fovEffects);
    file.setValue(u"mouseSensitivity"_s, mouseSensitivity);
    file.setValue(u"invertMouse"_s, invertMouse);
    debug.save(file);
    file.setValue(u"showLabels"_s, showLabels);
    file.setValue(u"photoHidesHand"_s, photoHidesHand);
    file.setValue(u"pauseOnFocusLoss"_s, pauseOnFocusLoss);
    file.setValue(u"language"_s, language);
    file.setValue(u"cameraMode"_s, QString::fromLatin1(QcViewCamera::name(cameraMode)));
    file.setValue(u"uiScale"_s, QcUiScale::name(uiScale));
    file.setValue(u"textureFiltering"_s, textureFiltering);

    QcCharacterProfile::saveAppearance(file, u"character"_s, appearance);
    QcCharacterProfile::savePresets(file, characterPresets);

    file.setValue(u"audio/master"_s, audio.master);
    file.setValue(u"audio/effects"_s, audio.effects);
    file.setValue(u"audio/music"_s, audio.music);
    file.setValue(u"audio/ambient"_s, audio.ambient);
    file.setValue(u"audio/muted"_s, audio.muted);
    gamepad.save(file);

    for (const QcKeyBindings::Action action : QcKeyBindings::actions()) {
        if (QcKeyBindings::isFixed(action)) {
            continue;
        }
        for (int slot = 0; slot < QcKeyBindings::slotCount; ++slot) {
            file.setValue(keySetting(action, slot), QcKeyBindings::keyName(keys.key(action, slot)));
        }
    }

    saveTexturePacks(file, texturePacks);

    file.sync();
    if (file.status() != QSettings::NoError) {
        return qcError(u"Cannot write the settings to %1"_s.arg(path));
    }
    return {};
}

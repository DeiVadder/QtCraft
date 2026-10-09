#include "QcGameSettings.h"

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

QcKeyBindings loadKeys(const QSettings &file)
{
    // setKey() ignores keys the platform cannot bind; those slots keep their default.
    QcKeyBindings keys = QcKeyBindings::defaults();
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
        }
    }
    keys.dropConflicts();
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

QcGameSettings QcGameSettings::load(const QString &path)
{
    QcGameSettings settings;
    if (!QFileInfo(path).isFile()) {
        return settings;
    }
    const QSettings file(path, QSettings::IniFormat);
    bool ok = false;
    const int view = file.value(u"viewDistance"_s).toInt(&ok);
    settings.viewDistance = ok ? clampViewDistance(view) : defaultViewDistance;
    const double fov = file.value(u"fieldOfView"_s).toDouble(&ok);
    settings.fieldOfView = ok ? clampFieldOfView(fov) : defaultFieldOfView;
    const double sensitivity = file.value(u"mouseSensitivity"_s).toDouble(&ok);
    settings.mouseSensitivity = ok ? clampMouseSensitivity(sensitivity) : defaultMouseSensitivity;
    settings.invertMouse = file.value(u"invertMouse"_s, false).toBool();
    settings.showDebug = file.value(u"showDebug"_s, true).toBool();
    settings.language = cleanLanguage(file.value(u"language"_s).toString());
    settings.cameraMode = QcViewCamera::fromName(file.value(u"cameraMode"_s).toString()).value_or(QcCameraMode::FirstPerson);
    const auto name = [&file](const QString &key, const QString &fallback) {
        const QVariant value = file.value(key);
        return value.isValid() && value.toString().size() <= QcAppearance::maxNameLength ? value.toString() : fallback;
    };
    const auto color = [&file](const QString &key, quint32 fallback) {
        const QString text = file.value(key).toString();
        bool ok = false;
        const quint32 rgb = text.startsWith(u'#') && text.size() == 7 ? QStringView(text).mid(1).toUInt(&ok, 16) : 0;
        return ok ? rgb : fallback;
    };
    const QcAppearance defaults;
    settings.appearance.outfit = name(u"character/outfit"_s, defaults.outfit);
    settings.appearance.hair = name(u"character/hair"_s, defaults.hair);
    settings.appearance.skinColor = color(u"character/skin"_s, defaults.skinColor);
    settings.appearance.hairColor = color(u"character/hairColor"_s, defaults.hairColor);
    settings.appearance.shirtColor = color(u"character/shirt"_s, defaults.shirtColor);
    settings.appearance.pantsColor = color(u"character/pants"_s, defaults.pantsColor);
    const auto volume = [&file](const QString &key, double fallback) {
        bool ok = false;
        const double value = file.value(key).toDouble(&ok);
        return ok ? clampVolume(value, fallback) : fallback;
    };
    settings.masterVolume = volume(u"audio/master"_s, defaultMasterVolume);
    settings.effectsVolume = volume(u"audio/effects"_s, defaultEffectsVolume);
    settings.musicVolume = volume(u"audio/music"_s, defaultMusicVolume);
    settings.ambientVolume = volume(u"audio/ambient"_s, defaultAmbientVolume);
    settings.muted = file.value(u"audio/muted"_s, false).toBool();
    settings.keys = loadKeys(file);
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
    file.setValue(u"mouseSensitivity"_s, mouseSensitivity);
    file.setValue(u"invertMouse"_s, invertMouse);
    file.setValue(u"showDebug"_s, showDebug);
    file.setValue(u"language"_s, language);
    file.setValue(u"cameraMode"_s, QString::fromLatin1(QcViewCamera::name(cameraMode)));
    const auto hex = [](quint32 rgb) { return u"#%1"_s.arg(uint(rgb & 0xFFFFFFu), 6, 16, QChar(u'0')); };
    file.setValue(u"character/outfit"_s, appearance.outfit);
    file.setValue(u"character/hair"_s, appearance.hair);
    file.setValue(u"character/skin"_s, hex(appearance.skinColor));
    file.setValue(u"character/hairColor"_s, hex(appearance.hairColor));
    file.setValue(u"character/shirt"_s, hex(appearance.shirtColor));
    file.setValue(u"character/pants"_s, hex(appearance.pantsColor));
    file.setValue(u"audio/master"_s, masterVolume);
    file.setValue(u"audio/effects"_s, effectsVolume);
    file.setValue(u"audio/music"_s, musicVolume);
    file.setValue(u"audio/ambient"_s, ambientVolume);
    file.setValue(u"audio/muted"_s, muted);
    for (const QcKeyBindings::Action action : QcKeyBindings::actions()) {
        if (QcKeyBindings::isFixed(action)) {
            continue;
        }
        for (int slot = 0; slot < QcKeyBindings::slotCount; ++slot) {
            file.setValue(keySetting(action, slot), QcKeyBindings::keyName(keys.key(action, slot)));
        }
    }
    file.sync();
    if (file.status() != QSettings::NoError) {
        return qcError(u"Cannot write the settings to %1"_s.arg(path));
    }
    return {};
}

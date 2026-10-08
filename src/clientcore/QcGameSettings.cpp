#include "QcGameSettings.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

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
    file.sync();
    if (file.status() != QSettings::NoError) {
        return qcError(u"Cannot write the settings to %1"_s.arg(path));
    }
    return {};
}

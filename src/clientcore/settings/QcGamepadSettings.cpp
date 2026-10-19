#include "clientcore/settings/QcGamepadSettings.h"

#include <QSettings>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// The models' profiles as the INI array "gamepadControllers": <n>/guid, <n>/name, <n>/<key>.
const QString controllersKey = u"gamepadControllers"_s;

QString controllerPrefix(qsizetype index)
{
    return u"%1/%2"_s.arg(controllersKey).arg(index + 1);
}

} // namespace

const QcGamepadProfile &QcGamepadSettings::profileFor(const QString &guid) const
{
    const Controller *own = controller(guid);
    return own ? own->profile : standard;
}

const QcGamepadSettings::Controller *QcGamepadSettings::controller(const QString &guid) const
{
    if (guid.isEmpty()) {
        return nullptr;
    }
    const auto it = std::ranges::find(controllers, guid, &Controller::guid);
    return it == controllers.end() ? nullptr : &*it;
}

void QcGamepadSettings::setProfile(const QString &guid, const QString &name, const QcGamepadProfile &profile, const QStringList &keep)
{
    if (guid.isEmpty()) {
        return;
    }

    Controller entry{guid, name, profile.sanitized()};
    const qsizetype index = std::ranges::find(std::as_const(controllers), guid, &Controller::guid) - controllers.cbegin();
    if (index < controllers.size()) {
        entry.name = controllers.at(index).name;
        controllers.removeAt(index);
    } else if (controllers.size() >= maxControllers) {
        const auto unkept = [&keep](const Controller &old) { return !keep.contains(old.guid); };
        const qsizetype evicted = std::ranges::find_if(std::as_const(controllers), unkept) - controllers.cbegin();
        controllers.removeAt(evicted < controllers.size() ? evicted : 0);
    }
    controllers.append(entry);
}

void QcGamepadSettings::removeProfile(const QString &guid)
{
    controllers.removeIf([&guid](const Controller &entry) { return entry.guid == guid; });
}

QcGamepadSettings QcGamepadSettings::load(const QSettings &file)
{
    QcGamepadSettings settings;
    settings.standard = QcGamepadProfile::load(file, u"gamepad"_s);
    settings.radialOnly = file.value(u"gamepad/radialOnly"_s, false).toBool();

    const qsizetype count = std::clamp<qsizetype>(file.value(controllersKey + u"/size"_s).toInt(), 0, maxControllers);
    for (qsizetype index = 0; index < count; ++index) {
        const QString prefix = controllerPrefix(index);
        const QString guid = file.value(prefix + u"/guid"_s).toString();
        // An entry without a model or for one seen before is dropped.
        if (!guid.isEmpty() && !settings.controller(guid)) {
            settings.controllers.append({guid, file.value(prefix + u"/name"_s).toString(), QcGamepadProfile::load(file, prefix)});
        }
    }
    return settings;
}

void QcGamepadSettings::save(QSettings &file) const
{
    standard.save(file, u"gamepad"_s);
    file.setValue(u"gamepad/radialOnly"_s, radialOnly);
    file.remove(controllersKey);
    file.setValue(controllersKey + u"/size"_s, controllers.size());
    for (qsizetype index = 0; index < controllers.size(); ++index) {
        const QString prefix = controllerPrefix(index);
        file.setValue(prefix + u"/guid"_s, controllers[index].guid);
        file.setValue(prefix + u"/name"_s, controllers[index].name);
        controllers[index].profile.save(file, prefix);
    }
}

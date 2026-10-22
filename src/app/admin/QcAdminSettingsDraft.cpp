#include "app/admin/QcAdminSettingsDraft.h"

#include <QLocale>

#include <algorithm>

using namespace Qt::StringLiterals;

QString QcAdminSettingsDraft::id(bool limit, const QString &key)
{
    return (limit ? u"limit:"_s : u"rule:"_s) + key;
}

const QcMsg::AdminSettings::Setting *QcAdminSettingsDraft::find(const QcMsg::AdminSettings &settings, bool limit, const QString &key)
{
    const QList<QcMsg::AdminSettings::Setting> &list = limit ? settings.limits : settings.rules;
    const auto found = std::ranges::find(list, key, &QcMsg::AdminSettings::Setting::key);
    return found == list.end() ? nullptr : &*found;
}

std::optional<qint64> QcAdminSettingsDraft::parsed(const QString &text, const QcMsg::AdminSettings::Setting &setting)
{
    bool ok = false;
    qint64 value = QLocale().toLongLong(text.trimmed(), &ok);
    if (!ok) {
        value = text.trimmed().toLongLong(&ok);
    }
    const qint64 low = setting.type == QcMsg::AdminSettings::Type::Boolean ? 0 : setting.low;
    const qint64 high = setting.type == QcMsg::AdminSettings::Type::Boolean ? 1 : setting.high;
    if (!ok || value < low || value > high) {
        return std::nullopt;
    }
    return value;
}

QVariantMap QcAdminSettingsDraft::values() const
{
    QVariantMap map;
    for (auto it = m_values.cbegin(); it != m_values.cend(); ++it) {
        map.insert(it.key(), it.value());
    }
    return map;
}

QList<QcAdminSettingsDraft::Entry> QcAdminSettingsDraft::entries() const
{
    QList<Entry> list;
    for (auto it = m_values.cbegin(); it != m_values.cend(); ++it) {
        const bool limit = it.key().startsWith(u"limit:"_s);
        list.append({limit, it.key().mid(it.key().indexOf(u':') + 1), it.value()});
    }
    return list;
}

void QcAdminSettingsDraft::change(const QcMsg::AdminSettings::Setting &setting, bool limit, qint64 value)
{
    if (value == setting.value) {
        m_values.remove(id(limit, setting.key));
    } else {
        m_values.insert(id(limit, setting.key), value);
    }
}

void QcAdminSettingsDraft::saved(const QString &id, qint64 value)
{
    if (const auto found = m_values.constFind(id); found != m_values.cend() && *found == value) {
        m_values.erase(found);
    }
}

void QcAdminSettingsDraft::prune(const QcMsg::AdminSettings &settings)
{
    const QList<Entry> all = entries();
    for (const Entry &entry : all) {
        const QcMsg::AdminSettings::Setting *setting = find(settings, entry.limit, entry.key);
        if (!setting || setting->value == entry.value) {
            m_values.remove(id(entry.limit, entry.key));
        }
    }
}

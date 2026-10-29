// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/settings/QcCharacterPresets.h"

#include <QCoreApplication>
#include <QEvent>

using namespace Qt::StringLiterals;

QcCharacterPresets::QcCharacterPresets(QObject *parent)
    : QObject(parent)
{
    // Installing or removing a translator sends LanguageChange to the application object.
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->installEventFilter(this);
    }
}

bool QcCharacterPresets::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == QCoreApplication::instance() && event->type() == QEvent::LanguageChange) {
        emit changed();
    }
    return QObject::eventFilter(watched, event);
}

QVariantList QcCharacterPresets::rows() const
{
    QVariantList list;
    for (const QcCharacterPreset &preset : QcCharacterProfile::builtInPresets()) {
        const QString label = QCoreApplication::translate("QcCharacterProfile", preset.name.toUtf8().constData());
        list.append(QVariantMap{{u"label"_s, label}, {u"builtIn"_s, true}, {u"look"_s, QcCharacterProfile::lookMap(preset.appearance)}});
    }
    for (const QcCharacterPreset &preset : m_saved) {
        list.append(QVariantMap{{u"label"_s, preset.name}, {u"builtIn"_s, false}, {u"look"_s, QcCharacterProfile::lookMap(preset.appearance)}});
    }
    return list;
}

void QcCharacterPresets::load(const QList<QcCharacterPreset> &presets)
{
    m_saved = presets.mid(0, QcCharacterProfile::maxPresets);
    emit changed();
}

std::optional<QcAppearance> QcCharacterPresets::appearance(int row) const
{
    const QList<QcCharacterPreset> &builtIn = QcCharacterProfile::builtInPresets();
    if (row >= 0 && row < builtIn.size()) {
        return builtIn[row].appearance;
    }
    if (row >= builtIn.size() && row - builtIn.size() < m_saved.size()) {
        return m_saved[row - builtIn.size()].appearance;
    }
    return std::nullopt;
}

qsizetype QcCharacterPresets::savedIndex(const QString &cleanName) const
{
    for (qsizetype index = 0; index < m_saved.size(); ++index) {
        if (m_saved[index].name == cleanName) {
            return index;
        }
    }
    return -1;
}

bool QcCharacterPresets::canSaveAs(const QString &name) const
{
    const QString clean = QcCharacterProfile::cleanPresetName(name);
    return !clean.isEmpty() && (canSave() || savedIndex(clean) >= 0);
}

bool QcCharacterPresets::save(const QString &name, const QcAppearance &look)
{
    if (!canSaveAs(name)) {
        return false;
    }

    const QString clean = QcCharacterProfile::cleanPresetName(name);
    if (const qsizetype index = savedIndex(clean); index >= 0) {
        m_saved[index].appearance = look;
    } else {
        m_saved.append({clean, look});
    }
    emit changed();
    return true;
}

void QcCharacterPresets::remove(int row)
{
    const qsizetype index = row - QcCharacterProfile::builtInPresets().size();
    if (index >= 0 && index < m_saved.size()) {
        m_saved.removeAt(index);
        emit changed();
    }
}

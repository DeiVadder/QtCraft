// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/settings/QcSoundVolumeModel.h"

#include "app/settings/QcAudioSettings.h"
#include "clientcore/settings/QcAudioVolumes.h"
#include "clientcore/sound/QcSoundCategory.h"

#include <QCoreApplication>

#include <array>
#include <optional>

namespace {

// The slider names in the order of QcSoundCategory, as in the role model's sound options.
constexpr std::array<const char *, qcSoundCategoryCount> categoryNames = {
    QT_TRANSLATE_NOOP("QcSoundVolumeModel", "Music"),
    QT_TRANSLATE_NOOP("QcSoundVolumeModel", "Weather"),
    QT_TRANSLATE_NOOP("QcSoundVolumeModel", "Blocks"),
    QT_TRANSLATE_NOOP("QcSoundVolumeModel", "Hostile creatures"),
    QT_TRANSLATE_NOOP("QcSoundVolumeModel", "Friendly creatures"),
    QT_TRANSLATE_NOOP("QcSoundVolumeModel", "Players"),
    QT_TRANSLATE_NOOP("QcSoundVolumeModel", "Ambience"),
    QT_TRANSLATE_NOOP("QcSoundVolumeModel", "Interface"),
};

} // namespace

QcSoundVolumeModel::QcSoundVolumeModel(const QcAudioSettings *settings, QObject *parent)
    : QAbstractListModel(parent)
    , m_settings(settings)
{
    connect(m_settings, &QcAudioSettings::changed, this, [this] { rowsChanged({VolumeRole}); });
}

int QcSoundVolumeModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : qcSoundCategoryCount;
}

QVariant QcSoundVolumeModel::data(const QModelIndex &index, int role) const
{
    const std::optional<QcSoundCategory> category = index.isValid() ? QcSoundCategories::fromIndex(index.row()) : std::nullopt;
    if (!category) {
        return {};
    }

    const auto row = static_cast<std::size_t>(index.row());
    switch (role) {
    case CategoryRole:
        return QVariant::fromValue(static_cast<QcAudioSettings::SoundCategory>(index.row()));
    case NameRole:
        return QCoreApplication::translate("QcSoundVolumeModel", categoryNames[row]);
    case VolumeRole:
        return m_settings->volumes().categories.volume(*category);
    case DefaultVolumeRole:
        return QcAudioVolumes::defaultVolume(*category);
    default:
        return {};
    }
}

QHash<int, QByteArray> QcSoundVolumeModel::roleNames() const
{
    return {{CategoryRole, "category"}, {NameRole, "name"}, {VolumeRole, "volume"}, {DefaultVolumeRole, "defaultVolume"}};
}

void QcSoundVolumeModel::retranslate()
{
    rowsChanged({NameRole});
}

void QcSoundVolumeModel::rowsChanged(const QList<int> &roles)
{
    emit dataChanged(index(0), index(rowCount() - 1), roles);
}

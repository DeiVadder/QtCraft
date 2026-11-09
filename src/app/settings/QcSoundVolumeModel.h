// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractListModel>
#include <QtQmlIntegration/qqmlintegration.h>

class QcAudioSettings;

// The volume sliders of the sound categories on the audio page (decision E-1150), one row per
// category in its order: `category` (AudioSettings.SoundCategory), `name` (translated), `volume`
// and `defaultVolume`. The rows never change; the volumes follow the settings, and the page sets
// them with AudioSettings.setVolume().
class QcSoundVolumeModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SoundVolumeModel)
    QML_UNCREATABLE("Provided by GameController.settings.audio.categories")

public:
    enum Role { CategoryRole = Qt::UserRole + 1, NameRole, VolumeRole, DefaultVolumeRole };

    // Rows for the categories of `settings`, which outlives the model.
    explicit QcSoundVolumeModel(const QcAudioSettings *settings, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    // The language changed: the names follow.
    void retranslate();

private:
    // Every row changed in `roles`.
    void rowsChanged(const QList<int> &roles);

    const QcAudioSettings *m_settings = nullptr;
};

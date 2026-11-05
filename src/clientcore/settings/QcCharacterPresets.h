// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/settings/QcCharacterProfile.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

/*! \brief The presets of the character screen (decision E-231): the built-in ones, translated,
    followed by the player's own (at most QcCharacterProfile::maxPresets).

    Rows count across both. The labels follow the UI language: \c {changed} is also emitted when
    a translator is installed or removed. */
class QcCharacterPresets : public QObject
{
    Q_OBJECT

public:
    explicit QcCharacterPresets(QObject *parent = nullptr);

    /*! Returns the rows for QML: [{label, builtIn, look}], look as QcCharacterProfile::lookMap. */
    QVariantList rows() const;
    /*! Returns the player's own presets. */
    const QList<QcCharacterPreset> &saved() const { return m_saved; }
    /*! Takes the player's own \a presets from the settings (at most
        QcCharacterProfile::maxPresets). */
    void load(const QList<QcCharacterPreset> &presets);

    /*! Returns the look of \a row; none for a row that does not exist. */
    [[nodiscard]] std::optional<QcAppearance> appearance(int row) const;
    /*! Returns whether there is a free place for another preset of the player. */
    bool canSave() const { return m_saved.size() < QcCharacterProfile::maxPresets; }
    /*! Returns whether save(\a name) would succeed: a usable name and a free place or a preset of
        that name to replace. */
    bool canSaveAs(const QString &name) const;
    /*! Saves \a look as the player's preset \a name (cleaned; replacing one of that name).
        Returns false when canSaveAs() refuses. */
    bool save(const QString &name, const QcAppearance &look);
    /*! Removes the preset of the player in \a row; built-in ones stay. */
    void remove(int row);

    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    /*! The rows changed: a preset saved, removed or loaded, or the UI language changed. */
    void changed();

private:
    qsizetype savedIndex(const QString &cleanName) const;

    QList<QcCharacterPreset> m_saved;
};

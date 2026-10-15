#pragma once

#include "QcCharacterProfile.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

// The presets of the character screen (decision E-231): the built-in ones, translated, followed by
// the player's own (at most QcCharacterProfile::maxPresets). Rows count across both. The labels
// follow the UI language: `changed` is also emitted when a translator is installed or removed.
class QcCharacterPresets : public QObject
{
    Q_OBJECT

public:
    explicit QcCharacterPresets(QObject *parent = nullptr);

    // [{label, builtIn, look}], look as QcCharacterProfile::lookMap.
    QVariantList rows() const;
    const QList<QcCharacterPreset> &saved() const { return m_saved; }
    void load(const QList<QcCharacterPreset> &presets);

    // The look of a row; none for a row that does not exist.
    [[nodiscard]] std::optional<QcAppearance> appearance(int row) const;
    // A free place for another preset of the player.
    bool canSave() const { return m_saved.size() < QcCharacterProfile::maxPresets; }
    // Whether save(name) would succeed: a usable name and a free place or a preset of that name
    // to replace.
    bool canSaveAs(const QString &name) const;
    // Saves `look` as the player's preset `name` (cleaned; replacing one of that name).
    bool save(const QString &name, const QcAppearance &look);
    // Removes a preset of the player; built-in ones stay.
    void remove(int row);

    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void changed();

private:
    qsizetype savedIndex(const QString &cleanName) const;

    QList<QcCharacterPreset> m_saved;
};

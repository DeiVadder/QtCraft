// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcAdminMessages.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QVariantMap>

#include <optional>

// The changed game rules and server limits of the "Manage server" screen before they are saved
// (decision E-863, docs/UI-Leitfaden.md 5, pattern 2). Kept by the controller, not the page, so a
// draft outlives the screen closing for another reason (death, the pause menu closing) and shows as
// unsaved when it opens again. A value the server already has leaves the draft.
class QcAdminSettingsDraft
{
public:
    struct Entry
    {
        bool limit = false;
        QString key;
        qint64 value = 0;
    };

    // The draft's id of a setting: "rule:" or "limit:" and the key.
    static QString id(bool limit, const QString &key);
    // The setting `key` of the rules (`limit` false) or limits; null when the server has none.
    static const QcMsg::AdminSettings::Setting *find(const QcMsg::AdminSettings &settings, bool limit, const QString &key);
    // `text` as a value of `setting`: an integer in the player's language or plain, within its range
    // (switches 0 or 1); std::nullopt otherwise. Never clamped.
    static std::optional<qint64> parsed(const QString &text, const QcMsg::AdminSettings::Setting &setting);

    bool isEmpty() const { return m_values.isEmpty(); }
    // {id: value} for QML.
    QVariantMap values() const;
    QList<Entry> entries() const;
    // Sets `value` of `setting` (checked by the caller); the server's own value removes it.
    void change(const QcMsg::AdminSettings::Setting &setting, bool limit, qint64 value);
    // The server took `value` for `id`: it leaves the draft unless it was changed again since.
    void saved(const QString &id, qint64 value);
    // Removes what the server has now or knows no more.
    void prune(const QcMsg::AdminSettings &settings);
    void clear() { m_values.clear(); }

private:
    QMap<QString, qint64> m_values;
};

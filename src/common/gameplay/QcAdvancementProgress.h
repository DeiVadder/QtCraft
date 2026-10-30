// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcAdvancementDefinition.h"

#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

/*! \brief The criteria one player has met, per advancement (decision E-923).

    Advancements and criteria the content does not know (a mod removed, a later version) are kept
    as they are, so they come back when the content has them again; whether an advancement is
    reached follows from its requirements (isDone). The server keeps it per player and saves it
    with the player file; the client keeps the local player's copy from the server's messages. */
class QcAdvancementProgress
{
public:
    /*! The most advancements a progress keeps (saving and reading); the criteria per advancement
        are bounded by QcAdvancementDefinition::maxCriteria. */
    static constexpr qsizetype maxAdvancements = 4096;
    /*! The longest name of an advancement or criterion it keeps. */
    static constexpr qsizetype maxNameLength = 128;

    /*! Marks \a criterion of \a advancement met. Returns false when it was already (or the
        bounds are full, or a name is too long). */
    bool grant(const QString &advancement, const QString &criterion);
    /*! Marks \a criterion of \a advancement unmet. Returns false when it was not met. */
    bool revoke(const QString &advancement, const QString &criterion);
    /*! Sets the met \a criteria of \a advancement (empty: none). Returns false when the bounds
        refuse it (a name too long, too many criteria, or no room for another advancement). */
    bool setCriteria(const QString &advancement, const QSet<QString> &criteria);

    /*! Returns the met criteria of \a advancement; empty for none. */
    [[nodiscard]] QSet<QString> criteria(const QString &advancement) const { return m_done.value(advancement); }
    /*! Returns whether \a criterion of \a advancement is met. */
    [[nodiscard]] bool has(const QString &advancement, const QString &criterion) const;
    /*! Returns whether the met criteria fulfil the requirements of \a advancement. */
    [[nodiscard]] bool isDone(const QcAdvancementDefinition &advancement) const { return advancement.isDone(m_done.value(advancement.name)); }
    /*! Returns the advancements with at least one met criterion, sorted. */
    [[nodiscard]] QStringList advancements() const;
    /*! Returns whether no criterion is met. */
    [[nodiscard]] bool isEmpty() const { return m_done.isEmpty(); }
    bool operator==(const QcAdvancementProgress &other) const = default;

    /*! Returns the saved form: a format version and the met criteria by advancement, sorted. */
    [[nodiscard]] QByteArray bytes() const;
    /*! Returns the progress saved in \a bytes; std::nullopt for damaged bytes; fields a later
        format appends are left out. */
    [[nodiscard]] static std::optional<QcAdvancementProgress> fromBytes(const QByteArray &bytes);

private:
    QHash<QString, QSet<QString>> m_done;  // NOLINT(qc-member-access): the whole state of the class; its methods are the accessors
};

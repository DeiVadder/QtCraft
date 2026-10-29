#pragma once

#include "common/content/definitions/QcAdvancementDefinition.h"

#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

// The criteria one player has met, per advancement (decision E-923). Advancements and criteria the
// content does not know (a mod removed, a later version) are kept as they are, so they come back
// when the content has them again; whether an advancement is reached follows from its requirements
// (isDone). The server keeps it per player and saves it with the player file; the client keeps the
// local player's copy from the server's messages.
class QcAdvancementProgress
{
public:
    // The most advancements and criteria per advancement a progress keeps (saving and reading).
    static constexpr qsizetype maxAdvancements = 4096;
    static constexpr qsizetype maxNameLength = 128;

    // Marks `criterion` of `advancement` met; false when it was already (or the bounds are full).
    bool grant(const QString &advancement, const QString &criterion);
    // Marks it unmet; false when it was not met.
    bool revoke(const QString &advancement, const QString &criterion);
    // Sets the met criteria of `advancement` (empty: none); false when the bounds refuse it (a name
    // too long, too many criteria, or no room for another advancement).
    bool setCriteria(const QString &advancement, const QSet<QString> &criteria);

    [[nodiscard]] QSet<QString> criteria(const QString &advancement) const { return m_done.value(advancement); }
    [[nodiscard]] bool has(const QString &advancement, const QString &criterion) const;
    [[nodiscard]] bool isDone(const QcAdvancementDefinition &advancement) const { return advancement.isDone(m_done.value(advancement.name)); }
    // The advancements with at least one met criterion, sorted.
    [[nodiscard]] QStringList advancements() const;
    [[nodiscard]] bool isEmpty() const { return m_done.isEmpty(); }
    bool operator==(const QcAdvancementProgress &other) const = default;

    // The saved form: a format version and the met criteria by advancement, sorted.
    [[nodiscard]] QByteArray bytes() const;
    // std::nullopt for damaged bytes; fields a later format appends are left out.
    [[nodiscard]] static std::optional<QcAdvancementProgress> fromBytes(const QByteArray &bytes);

private:
    QHash<QString, QSet<QString>> m_done;  // NOLINT(qc-member-access): the whole state of the class; its methods are the accessors
};

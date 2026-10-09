#pragma once

#include <QString>

// Identity of a content pack that went into a registry (base game or mod); shown in the UI and
// recorded with saved worlds.
struct QcPackReference
{
    QString id;
    QString name;
    QString version;

    friend bool operator==(const QcPackReference &, const QcPackReference &) = default;
};

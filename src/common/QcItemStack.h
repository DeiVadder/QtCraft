#pragma once

#include "QcItemDefinition.h"

// A number of identical items, e.g. one inventory slot. `damage` counts tool uses.
struct QcItemStack
{
    QcItemId item = qcNoItem;
    int count = 0;
    int damage = 0;

    bool isEmpty() const { return item == qcNoItem || count <= 0; }
    bool operator==(const QcItemStack &) const = default;
};

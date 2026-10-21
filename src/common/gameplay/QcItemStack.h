#pragma once

#include "common/content/definitions/QcItemDefinition.h"

// A number of identical items, e.g. one inventory slot. `damage` counts tool uses, or is the map id
// of a filled map (decision E-798).
struct QcItemStack
{
    QcItemId item = qcNoItem;
    int count = 0;
    int damage = 0;

    bool isEmpty() const { return item == qcNoItem || count <= 0; }
    // Whether the items of both stacks are the same and may go into one stack (up to maxStack):
    // the same item with the same damage (decision E-798). Items that wear out stack to one, so
    // worn tools never merge; filled maps of one map id do.
    bool stacksWith(QcItemStack other) const { return item == other.item && damage == other.damage; }
    bool operator==(const QcItemStack &) const = default;
};

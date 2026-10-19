#pragma once

#include "clientcore/weather/QcPrecipitationField.h"

#include <QList>

#include <array>
#include <memory>

// The lists of precipitation drops handed to the view in turn (decision E-397): the view keeps the
// latest, the renderer the one it last copied, so a third is always free to be filled again and a
// frame allocates nothing once the lists have grown.
//
// Thread contract: fill() runs on the GUI thread and hands the list to the view (QcWorldView::
// setPrecipitation). The renderer copies the view's pointer only in its synchronize(), while the GUI
// thread waits, and keeps it for its reads in the frame until the next synchronize() replaces it.
// Outside synchronize() another thread can therefore only drop a reference, never take one: a list
// whose only owner is the pool stays unshared while fill() writes it.
class QcDropPool
{
public:
    // The particles of `field` as drops (QcPrecipitationField::dropOf) in a list nobody else holds
    // (asserted in debug builds); a new list when all three are still held.
    [[nodiscard]] std::shared_ptr<const QList<QcPrecipitationDrop>> fill(const QcPrecipitationField &field, float strength);

private:
    std::array<std::shared_ptr<QList<QcPrecipitationDrop>>, 3> m_lists;
};

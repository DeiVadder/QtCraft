#include "QcShowcaseTour.h"

#include "QcShowcasePlan.h"

using namespace Qt::StringLiterals;

namespace QcShowcaseTour {

QStringList lines(const QcShowcasePlan &plan)
{
    QStringList result;
    for (const QcShowcaseTourStop &stop : plan.tour) {
        result.append(u"%1 %2 %3 %4 %5"_s.arg(stop.name).arg(stop.x).arg(stop.z).arg(stop.yaw, 0, 'f', 1).arg(stop.pitch, 0, 'f', 1));
    }
    return result;
}

} // namespace QcShowcaseTour

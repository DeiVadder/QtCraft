#include "clientcore/input/QcGamepadTypeDetection.h"

#include <QStringList>

using namespace Qt::StringLiterals;

namespace {

constexpr quint16 vendorMicrosoft = 0x045e;
constexpr quint16 vendorSony = 0x054c;
constexpr quint16 vendorNintendo = 0x057e;
// Nintendo's Switch Pro Controller and the Joy-Con pair used as one pad.
constexpr quint16 productSwitchPro = 0x2009;
constexpr quint16 productJoyConPair = 0x200e;

bool nameContains(const QString &name, const QStringList &needles)
{
    for (const QString &needle : needles) {
        if (name.contains(needle, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

} // namespace

QcGamepadType QcGamepadTypeDetection::detect(quint16 vendor, quint16 product, const QString &name)
{
    if (vendor == vendorMicrosoft) {
        return QcGamepadType::Xbox;
    }
    if (vendor == vendorSony) {
        return QcGamepadType::PlayStation;
    }
    if (vendor == vendorNintendo) {
        // A single Joy-Con, the NES/SNES/N64 pads and unknown products have no Switch Pro layout.
        const bool proLayout = product == productSwitchPro || product == productJoyConPair;
        return proLayout ? QcGamepadType::SwitchPro : QcGamepadType::Generic;
    }

    if (nameContains(name, {u"xbox"_s, u"xinput"_s})) {
        return QcGamepadType::Xbox;
    }
    if (nameContains(name, {u"dualsense"_s, u"dualshock"_s, u"ps3"_s, u"ps4"_s, u"ps5"_s, u"playstation"_s})) {
        return QcGamepadType::PlayStation;
    }
    // A single Joy-Con named by its maker without Nintendo's vendor id stays Generic too.
    if (nameContains(name, {u"joy-con (l)"_s, u"joy-con (r)"_s})) {
        return QcGamepadType::Generic;
    }
    if (nameContains(name, {u"pro controller"_s, u"switch"_s})) {
        return QcGamepadType::SwitchPro;
    }
    return QcGamepadType::Generic;
}

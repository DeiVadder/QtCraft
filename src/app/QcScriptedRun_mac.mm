#include "QcScriptedRun.h"

#import <AppKit/AppKit.h>
#include <ApplicationServices/ApplicationServices.h>

#include <array>

QList<QRect> QcScriptedRun::builtInDisplays()
{
    std::array<CGDirectDisplayID, 16> displays{};
    uint32_t count = 0;
    QList<QRect> result;
    if (CGGetActiveDisplayList(uint32_t(displays.size()), displays.data(), &count) != kCGErrorSuccess) {
        return result;
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (CGDisplayIsBuiltin(displays[i])) {
            const CGRect bounds = CGDisplayBounds(displays[i]);
            result.append(QRect(int(bounds.origin.x), int(bounds.origin.y), int(bounds.size.width), int(bounds.size.height)));
        }
    }
    return result;
}

void QcScriptedRun::stayInBackground()
{
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
}

#include "app/hooks/QcScriptedRun.h"

#import <AppKit/AppKit.h>
#include <ApplicationServices/ApplicationServices.h>

#include <QByteArray>
#include <QString>

#include <array>

using namespace Qt::StringLiterals;

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

void QcScriptedRun::keepOutOfForeground(int argc, char *argv[])
{
    bool background = qEnvironmentVariable("QTCRAFT_OFFSCREEN") == u"1"_s || qEnvironmentVariable("QTCRAFT_BACKGROUND") == u"1"_s;
    for (int i = 1; i < argc && !background; ++i) {
        background = qstrcmp(argv[i], "--background") == 0;
    }

    // Read by the Cocoa platform plugin when the application object is created.
    if (background) {
        qputenv("QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM", "1");
    }
}

void QcScriptedRun::stayInBackground()
{
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
}

void QcScriptedRun::keepTimersRunning()
{
    // Retained and held until the process ends.
    static const id activity = [[[NSProcessInfo processInfo] beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical
                                                                               reason:@"Scripted offscreen run"] retain];
    (void)activity;
}

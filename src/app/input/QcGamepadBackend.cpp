#include "app/input/QcGamepadBackend.h"

#include "clientcore/input/QcGamepadSource.h"

#if QC_HAVE_SDL_GAMEPAD
#include "app/input/QcSdlGamepadSource.h"
#endif

bool QcGamepadBackend::compiledIn()
{
    return QC_HAVE_SDL_GAMEPAD != 0;
}

std::unique_ptr<QcGamepadSource> QcGamepadBackend::create()
{
#if QC_HAVE_SDL_GAMEPAD
    auto source = std::make_unique<QcSdlGamepadSource>();
    if (source->isAvailable()) {
        return source;
    }
#endif
    return nullptr;
}

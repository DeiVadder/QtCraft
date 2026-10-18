#pragma once

#include <memory>

class QcGamepadSource;

// The gamepad backend of the game (decision E-540): SDL3 when the build has it (CMake option
// QC_GAMEPAD), else none. The only place that knows whether SDL is compiled in, so the rest of the
// client depends on QcGamepadSource alone.
namespace QcGamepadBackend {
// Whether this build contains the SDL3 backend.
bool compiledIn();
// The SDL3 source, polling on a Qt timer of the calling thread, owned by the caller (no QObject
// parent); nullptr when the build has no SDL or SDL did not start (logged under qtcraft.gamepad).
std::unique_ptr<QcGamepadSource> create();
}

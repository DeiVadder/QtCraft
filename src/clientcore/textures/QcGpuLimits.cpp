#include "clientcore/textures/QcGpuLimits.h"

#include <atomic>

namespace QcGpuLimits {

namespace {

std::atomic<int> reported{0};

} // namespace

int maxTextureSize()
{
    const int value = reported.load(std::memory_order_relaxed);
    return value > 0 ? value : assumedMaxTextureSize;
}

void report(int maxTextureSize)
{
    if (maxTextureSize > 0) {
        reported.store(maxTextureSize, std::memory_order_relaxed);
    }
}

} // namespace QcGpuLimits

#pragma once

// Memory of the running process in megabytes (debug display, statistics log, benchmark); 0 where
// the platform does not report it.
namespace QcProcessMemory {

// Resident memory now.
double currentMb();
// Highest resident memory so far.
double peakMb();

} // namespace QcProcessMemory

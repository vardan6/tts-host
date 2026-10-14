#include "tts_host/process_stats.hpp"

#ifdef _WIN32
#include <windows.h>

#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace tts_host {

std::uint64_t peak_resident_set_size_bytes() {
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS counters{};
  if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
    return 0;
  }
  return static_cast<std::uint64_t>(counters.PeakWorkingSetSize);
#else
  struct rusage usage {};
  if (getrusage(RUSAGE_SELF, &usage) != 0) {
    return 0;
  }
#ifdef __APPLE__
  // macOS reports ru_maxrss in bytes; every other POSIX platform reports it
  // in kilobytes.
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024;
#endif
#endif
}

}  // namespace tts_host

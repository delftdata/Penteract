#pragma once

#include <glog/logging.h>

#include <chrono>
#include <thread>

namespace slog {

inline void SetThreadName(pthread_t thread, const char* name) { 
#ifdef __APPLE__
  // macOS pthread_setname_np only works on the current thread, so we skip it.
#else
  pthread_setname_np(thread, name); 
#endif
}

inline void PinToCpu(pthread_t thread, int cpu) {
#ifdef __APPLE__
  // macOS does not support thread affinity pinning natively via pthread.
#else
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(cpu, &cpuset);
  int rc = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
  if (rc != 0) {
    LOG(ERROR) << "Failed to pin thread to CPU " << cpu << ". Error code: " << rc;
  }
#endif
}

}  // namespace slog
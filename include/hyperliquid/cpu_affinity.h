#pragma once

#include <thread>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#elif defined(__APPLE__)
#include <mach/thread_act.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#endif

namespace hyperliquid {

/// Pin a thread to a specific CPU core
/// Returns true on success, false on failure
inline bool pin_thread_to_core(unsigned int core_id) {
#if defined(__linux__)
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core_id, &cpuset);

  pthread_t current_thread = pthread_self();
  return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset) ==
         0;

#elif defined(__APPLE__)
  thread_affinity_policy_data_t policy = {static_cast<integer_t>(core_id)};
  thread_port_t mach_thread = pthread_mach_thread_np(pthread_self());

  kern_return_t result =
      thread_policy_set(mach_thread, THREAD_AFFINITY_POLICY,
                        (thread_policy_t)&policy, THREAD_AFFINITY_POLICY_COUNT);

  return result == KERN_SUCCESS;

#else
  // Platform not supported - graceful degradation
  (void)core_id;
  return false;
#endif
}

/// Pin the current std::thread to a specific CPU core
inline bool pin_this_thread(unsigned int core_id) {
  return pin_thread_to_core(core_id);
}

/// Get the number of available CPU cores
inline unsigned int get_num_cores() {
  return std::thread::hardware_concurrency();
}

} // namespace hyperliquid

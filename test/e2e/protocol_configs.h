#pragma once

#include "common/proto_utils.h"

namespace slog {

// Base internal::Configuration for each of the five protocols under test.
// Used by both singleregion_test_utils.h and multiregion_test_utils.h so the
// per-protocol settings are defined exactly once.

inline internal::Configuration DetockBaseConfig() {
  internal::Configuration cfg;
  cfg.set_bypass_mh_orderer(true);
  cfg.set_synchronized_batching(true);
  cfg.set_ddr_interval(100);
  cfg.set_timestamp_buffer_us(2000);
  return cfg;
}

inline internal::Configuration DDROnlyBaseConfig() {
  internal::Configuration cfg;
  cfg.set_bypass_mh_orderer(true);
  cfg.set_synchronized_batching(true);
  cfg.set_ddr_interval(100);
  return cfg;
}

inline internal::Configuration SlogBaseConfig() {
  internal::Configuration cfg;
  cfg.set_bypass_mh_orderer(false);
  return cfg;
}

// Calvin uses the same module stack as SLOG; listed separately for test naming.
inline internal::Configuration CalvinBaseConfig() {
  internal::Configuration cfg;
  cfg.set_bypass_mh_orderer(false);
  return cfg;
}

inline internal::Configuration JanusBaseConfig() {
  internal::Configuration cfg;
  cfg.set_num_workers(6);
  return cfg;
}

}  // namespace slog

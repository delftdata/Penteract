#pragma once

#include <string>
#include <vector>

#include "execution/write_skew/storage_adapter.h"

namespace slog {
namespace write_skew {

struct ConsistencyCheckResult {
  std::string name;
  int violations = 0;
  std::vector<std::string> samples;

  bool passed() const { return violations == 0; }
};

// Number of write-skew probe pairs. Must match kPairs in isolation tests.
constexpr int kWriteSkewProbeSize = 100;

// Runs the write-skew consistency check.
//
// Each pair (id_a = 2*i-1, id_b = 2*i) for i in [1, kWriteSkewProbeSize]
// corresponds to one concurrent txn pair:
//   txn A writes id_a and reads id_b; txn B writes id_b and reads id_a.
// Both rows having VALUE=1 means both saw v1+v2 < 1 concurrently — write skew.
//
// Returns a vector with a single result named "write_skew_detected".
std::vector<ConsistencyCheckResult> RunWriteSkewCheck(const StorageAdapterPtr& storage_adapter,
                                                      int max_samples = 5);

enum class IsolationLevel {
  kReadCommittedOrWeaker,
  kSnapshotIsolation,
  kOrderingAnomaly,
  kStrictSerializable,
};

std::string IsolationLevelName(IsolationLevel level);

// Classifies isolation level based on write-skew check results.
// result[0] is the "write_skew_detected" entry.
IsolationLevel ClassifyIsolationLevel(const std::vector<ConsistencyCheckResult>& results);

}  // namespace write_skew
}  // namespace slog

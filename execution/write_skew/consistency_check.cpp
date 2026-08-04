#include "execution/write_skew/consistency_check.h"

#include <sstream>

#include "execution/write_skew/table.h"

namespace slog {
namespace write_skew {

namespace {

template <typename... Args>
std::string FormatSample(Args&&... args) {
  std::ostringstream out;
  (out << ... << args);
  return out.str();
}

void AddViolation(ConsistencyCheckResult& result, int max_samples, std::string sample) {
  result.violations++;
  if (static_cast<int>(result.samples.size()) < max_samples) {
    result.samples.push_back(std::move(sample));
  }
}

}  // namespace

std::vector<ConsistencyCheckResult> RunWriteSkewCheck(const StorageAdapterPtr& storage_adapter, int max_samples) {
  std::vector<ConsistencyCheckResult> results = {
      // Write-skew isolation probe.
      // A serializable system must never allow both v1 and v2 to be
      // incremented concurrently when v1+v2 < 1 is the invariant.
      // Source: Berenson et al., "A Critique of ANSI SQL Isolation Levels",
      // SIGMOD 1995, §4.2 "Write Skew Anomaly".
      {.name = "write_skew_detected"},
  };

  Table<WriteSkewSchema> table(storage_adapter);

  // Each pair (id_a=2i-1, id_b=2i) corresponds to one concurrent txn pair:
  //   txn A writes id_a and reads id_b; txn B writes id_b and reads id_a.
  // Both rows having VALUE=1 means both saw v1+v2 < 1 concurrently — write skew.
  for (int i = 1; i <= kWriteSkewProbeSize; i++) {
    int id_a = 2 * i - 1;
    int id_b = 2 * i;
    auto row_a = table.Select({MakeInt32Scalar(id_a)}, {WriteSkewSchema::Column::VALUE});
    auto row_b = table.Select({MakeInt32Scalar(id_b)}, {WriteSkewSchema::Column::VALUE});
    if (row_a.empty() || row_b.empty()) continue;
    int v1 = UncheckedCast<Int32Scalar>(row_a[0])->value;
    int v2 = UncheckedCast<Int32Scalar>(row_b[0])->value;
    if (v1 + v2 > 1) {
      AddViolation(results[0], max_samples,
                   FormatSample("Write Skew at pair ", i, " v1(id=", id_a, ")=", v1, " v2(id=", id_b, ")=", v2));
    }
  }

  if (results[0].violations > 0) {
    LOG(INFO) << "WriteSkew Check: Detected WRITE SKEW. System provides SNAPSHOT ISOLATION or lower.";
  } else {
    LOG(INFO) << "WriteSkew Check: No write skew detected. System provides STRICT SERIALIZABILITY (for this probe).";
  }

  return results;
}

std::string IsolationLevelName(IsolationLevel level) {
  switch (level) {
    case IsolationLevel::kReadCommittedOrWeaker: return "ReadCommittedOrWeaker";
    case IsolationLevel::kSnapshotIsolation:     return "SnapshotIsolation";
    case IsolationLevel::kOrderingAnomaly:       return "OrderingAnomaly";
    case IsolationLevel::kStrictSerializable:    return "StrictSerializable";
  }
  return "Unknown";
}

IsolationLevel ClassifyIsolationLevel(const std::vector<ConsistencyCheckResult>& results) {
  // results[0] is "write_skew_detected"
  if (!results.empty() && results[0].violations > 0) {
    return IsolationLevel::kSnapshotIsolation;
  }
  return IsolationLevel::kStrictSerializable;
}

}  // namespace write_skew
}  // namespace slog

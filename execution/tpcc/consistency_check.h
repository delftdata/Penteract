#pragma once

#include <string>
#include <vector>

#include "execution/tpcc/storage_adapter.h"

namespace slog {
namespace tpcc {

struct ConsistencyCheckResult {
  std::string name;
  int violations = 0;
  std::vector<std::string> samples;

  bool passed() const { return violations == 0; }
};

// Runs the TPC-C consistency checks defined in the TPC-C specification v5.11
// (Transaction Processing Performance Council, 2010), §3.3 "Consistency
// Conditions".
//
// TPC-C §3.3 conditions implemented:
//   C1  – warehouse_ytd_matches_districts                (§3.3.2.1)
//   C2  – district_next_order_matches_orders             (§3.3.2.2)
//   C3  – new_order_forms_contiguous_suffix              (§3.3.2.3)
//   C4  – district_order_line_sum_matches_headers        (§3.3.2.4)
//   C5  – order_delivery_state_matches_new_order         (§3.3.2.5)
//   C6  – order_line_count_matches_order_header          (§3.3.2.6)
//   C7  – order_line_delivery_matches_order              (§3.3.2.7)
//   C8  – warehouse_ytd_matches_history                  (§3.3.2.8)
//   C9  – district_ytd_matches_history                   (§3.3.2.9)
//   C10 – customer_balance_matches_delivered_orders_and_history (§3.3.2.10)
//   C11 – order_minus_new_order_equals_2100              (§3.3.2.11)
//   C12 – customer_balance_plus_ytd_matches_delivered_orders (§3.3.2.12)
std::vector<ConsistencyCheckResult> RunBasicConsistencyChecks(const StorageAdapterPtr& storage_adapter,
                                                             int warehouse_count,
                                                             int max_samples = 5);

}  // namespace tpcc
}  // namespace slog

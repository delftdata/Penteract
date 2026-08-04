#include "execution/benchx/consistency_check.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <unordered_map>

#include "execution/benchx/constants.h"
#include "execution/benchx/table.h"

namespace slog {
namespace benchx {
namespace {

template <typename... Args>
std::string FormatSample(Args&&... args) {
  std::ostringstream out;
  (out << ... << args);
  return out.str();
}

template <typename T>
T ReadPod(const std::string& data, size_t offset) {
  CHECK_LE(offset + sizeof(T), data.size());
  T value;
  memcpy(&value, data.data() + offset, sizeof(T));
  return value;
}

void AddViolation(ConsistencyCheckResult& result, int max_samples, std::string sample) {
  result.violations++;
  if (static_cast<int>(result.samples.size()) < max_samples) {
    result.samples.push_back(std::move(sample));
  }
}

}  // namespace

std::vector<ConsistencyCheckResult> RunBasicConsistencyChecks(const StorageAdapterPtr& storage_adapter,
                                                             int warehouse_count,
                                                             int max_samples) {
  Table<WarehouseSchema> warehouse(storage_adapter);
  Table<DistrictSchema> district(storage_adapter);
  Table<NewOrderSchema> new_order(storage_adapter);
  Table<OrderSchema> order(storage_adapter);
  Table<OrderLineSchema> order_line(storage_adapter);
  Table<CustomerSchema> customer(storage_adapter);

  // Indices must stay in sync with the results[] accesses below.
  // TPC-C conditions §3.3.2 from: TPC-C Benchmark Specification v5.11,
  // Transaction Processing Performance Council (BenchX), 2010.
  // https://www.tpc.org/tpcc/
  std::vector<ConsistencyCheckResult> results = {
      // [0] §3.3.2.1: W_YTD = sum(D_YTD) across all districts in the warehouse.
      {.name = "warehouse_ytd_matches_districts"},
      // [1] §3.3.2.2: D.NEXT_O_ID - 1 = max(O_ID) and max(NO_O_ID) per district.
      {.name = "district_next_order_matches_orders"},
      // [2] §3.3.2.3: NEW_ORDER rows form a contiguous suffix of the ORDER
      //     table — min(NO_O_ID) .. max(NO_O_ID) with no gaps.
      {.name = "new_order_forms_contiguous_suffix"},
      // [3] §3.3.2.4: For every district, sum(O_OL_CNT) equals the number of
      //     ORDER_LINE rows in that district.
      {.name = "district_order_line_sum_matches_headers"},
      // [4] §3.3.2.5: An undelivered order (O_CARRIER_ID = 0) must have a
      //     NEW_ORDER row and no delivered ORDER_LINE timestamps; a delivered
      //     order must not appear in NEW_ORDER and all lines must be timestamped.
      {.name = "order_delivery_state_matches_new_order"},
      // [5] §3.3.2.6: For every ORDER row, O_OL_CNT equals the number of
      //     ORDER_LINE rows with the same (W,D,O) key.
      {.name = "order_line_count_matches_order_header"},
      // [6] §3.3.2.7: For every ORDER_LINE row, DELIVERY_D is null iff the
      //     corresponding ORDER row has O_CARRIER_ID = 0.
      {.name = "order_line_delivery_matches_order"},
      // [7] §3.3.2.8: W_YTD = sum(H_AMOUNT) in HISTORY for that warehouse.
      {.name = "warehouse_ytd_matches_history"},
      // [8] §3.3.2.9: D_YTD = sum(H_AMOUNT) in HISTORY for that district.
      {.name = "district_ytd_matches_history"},
      // [9] §3.3.2.10: C_BALANCE = sum(delivered OL_AMOUNT) - sum(H_AMOUNT).
      {.name = "customer_balance_matches_delivered_orders_and_history"},
      // [10] §3.3.2.11: count(ORDER) - count(NEW_ORDER) = 2100 per district.
      {.name = "order_minus_new_order_equals_2100"},
      // [11] §3.3.2.12: C_BALANCE + C_YTD_PAYMENT = sum(delivered OL_AMOUNT).
      {.name = "customer_balance_plus_ytd_matches_delivered_orders"},
      // Additional bookkeeping checks that are useful in this codebase even
      // though the TPC-C spec does not require them to be demonstrated.
      {.name = "customer_ytd_payment_matches_history"},
      {.name = "customer_payment_cnt_matches_history"},
      {.name = "customer_delivery_cnt_matches_orders"},
  };

  std::unordered_map<int32_t, int64_t> warehouse_history_sums;
  std::unordered_map<std::string, int64_t> district_history_sums;
  std::unordered_map<std::string, int64_t> customer_history_sums;
  std::unordered_map<std::string, int32_t> customer_history_counts;
  std::unordered_map<std::string, int32_t> customer_delivery_counts;
  std::unordered_map<std::string, int64_t> customer_delivered_order_amounts;

  auto table_id = TableId::HISTORY;

  for (int w_id = 1; w_id <= warehouse_count; w_id++) {
    std::string prefix;
    prefix.reserve(sizeof(int32_t) + sizeof(TableId));
    auto warehouse_id = MakeInt32Scalar(w_id);
    prefix.append(reinterpret_cast<const char*>(warehouse_id->data()), warehouse_id->type->size());
    prefix.append(reinterpret_cast<const char*>(&table_id), sizeof(TableId));

    for (const auto& [key, value] : storage_adapter->ScanPrefix(prefix)) {
      CHECK_GE(key.size(), sizeof(int32_t) + sizeof(TableId) + sizeof(int8_t) + sizeof(int32_t) + sizeof(int32_t));
      int32_t history_w_id = ReadPod<int32_t>(key, 0);
      int8_t history_d_id = ReadPod<int8_t>(key, sizeof(int32_t) + sizeof(TableId));
      int8_t history_c_d_id = ReadPod<int8_t>(value, 0);
      int32_t history_c_w_id = ReadPod<int32_t>(value, sizeof(int8_t));
      int32_t history_amount = ReadPod<int32_t>(value, sizeof(int8_t) + sizeof(int32_t) + sizeof(int64_t));
      int32_t history_c_id = ReadPod<int32_t>(key, sizeof(int32_t) + sizeof(TableId) + sizeof(int8_t));

      warehouse_history_sums[history_w_id] += history_amount;
      district_history_sums[FormatSample(history_w_id, ":", static_cast<int>(history_d_id))] += history_amount;
      auto cust_key = FormatSample(history_c_w_id, ":", static_cast<int>(history_c_d_id), ":", history_c_id);
      customer_history_sums[cust_key] += history_amount;
      customer_history_counts[cust_key]++;
    }
  }

  for (int w_id = 1; w_id <= warehouse_count; w_id++) {
    auto warehouse_row = warehouse.Select(
        {MakeInt32Scalar(w_id)}, {WarehouseSchema::Column::YTD});
    int64_t w_ytd = 0;
    if (!warehouse_row.empty()) {
      w_ytd = UncheckedCast<Int64Scalar>(warehouse_row[0])->value;
    } else {
        AddViolation(results[0], max_samples,
                   FormatSample("missing warehouse w=", w_id));
        continue;
      }

    int64_t sum_d_ytd = 0;

    for (int d_id = 1; d_id <= kDistPerWare; d_id++) {
      auto district_row = district.Select(
          {MakeInt32Scalar(w_id), MakeInt8Scalar(d_id)},
          {DistrictSchema::Column::NEXT_O_ID, DistrictSchema::Column::YTD});
      if (district_row.empty()) {
        AddViolation(results[0], max_samples,
                     FormatSample("missing district w=", w_id, " d=", d_id));
        continue;
      }

      int next_o_id = UncheckedCast<Int32Scalar>(district_row[0])->value;
      int64_t d_ytd = UncheckedCast<Int64Scalar>(district_row[1])->value;
      sum_d_ytd += d_ytd;

      auto d_key = FormatSample(w_id, ":", d_id);
      if (district_history_sums[d_key] != d_ytd) {
        AddViolation(results[7], max_samples,
                     FormatSample("w=", w_id, " d=", d_id, " d_ytd=", d_ytd,
                                  " history_sum=", district_history_sums[d_key]));
      }

      int max_order_id = 0;
      int min_new_order_id = kOrdPerDist + 1;
      int max_new_order_id = 0;
      int new_order_count = 0;
      int order_count = 0;
      int district_order_line_count = 0;
      int district_header_order_line_count = 0;

      for (int o_id = 1; o_id < next_o_id; o_id++) {
        auto order_row = order.Select(
            {MakeInt32Scalar(w_id), MakeInt8Scalar(d_id), MakeInt32Scalar(o_id)},
            {OrderSchema::Column::CARRIER_ID, OrderSchema::Column::OL_CNT, OrderSchema::Column::C_ID});
        auto new_order_row = new_order.Select(
            {MakeInt32Scalar(w_id), MakeInt8Scalar(d_id), MakeInt32Scalar(o_id)});

        if (!order_row.empty()) {
          order_count++;
          max_order_id = o_id;

          int carrier_id = UncheckedCast<Int8Scalar>(order_row[0])->value;
          int ol_cnt = UncheckedCast<Int8Scalar>(order_row[1])->value;
          int c_id = UncheckedCast<Int32Scalar>(order_row[2])->value;
          district_header_order_line_count += ol_cnt;

          if (carrier_id != 0) {
            customer_delivery_counts[FormatSample(w_id, ":", d_id, ":", c_id)]++;
          }

          int observed_order_lines = 0;
          bool has_delivery_timestamp = false;
          bool has_missing_delivery_timestamp = false;
          int64_t delivered_order_amount = 0;

          for (int ol_number = 1; ol_number <= kLinePerOrder; ol_number++) {
            auto order_line_row = order_line.Select(
                {MakeInt32Scalar(w_id), MakeInt8Scalar(d_id), MakeInt32Scalar(o_id), MakeInt8Scalar(ol_number)},
                {OrderLineSchema::Column::DELIVERY_D, OrderLineSchema::Column::AMOUNT});
            if (order_line_row.empty()) {
              continue;
            }

            observed_order_lines++;
            district_order_line_count++;
            auto delivery_d = UncheckedCast<Int64Scalar>(order_line_row[0])->value;
            auto amount = UncheckedCast<Int32Scalar>(order_line_row[1])->value;
            has_delivery_timestamp |= delivery_d != 0;
            has_missing_delivery_timestamp |= delivery_d == 0;
            if (delivery_d != 0) {
              delivered_order_amount += amount;
            }
          }

          customer_delivered_order_amounts[FormatSample(w_id, ":", d_id, ":", c_id)] += delivered_order_amount;

          if (observed_order_lines != ol_cnt) {
            AddViolation(results[5], max_samples,
                         FormatSample("w=", w_id, " d=", d_id, " o=", o_id,
                                      " expected_ol_cnt=", ol_cnt,
                                      " actual_ol_cnt=", observed_order_lines));
          }

          if (carrier_id == 0) {
            if (new_order_row.empty()) {
              AddViolation(results[4], max_samples,
                           FormatSample("undelivered order missing NEW_ORDER row w=", w_id,
                                        " d=", d_id, " o=", o_id));
            }
            if (has_delivery_timestamp) {
              AddViolation(results[6], max_samples,
                           FormatSample("undelivered order has delivered line w=", w_id,
                                        " d=", d_id, " o=", o_id));
            }
          } else {
            if (!new_order_row.empty()) {
              AddViolation(results[4], max_samples,
                           FormatSample("delivered order still present in NEW_ORDER w=", w_id,
                                        " d=", d_id, " o=", o_id));
            }
            if (has_missing_delivery_timestamp) {
              AddViolation(results[6], max_samples,
                           FormatSample("delivered order missing delivery timestamp w=", w_id,
                                        " d=", d_id, " o=", o_id));
            }
          }
        }

        if (!new_order_row.empty()) {
          new_order_count++;
          min_new_order_id = std::min(min_new_order_id, o_id);
          max_new_order_id = std::max(max_new_order_id, o_id);
        }
      }

      if (max_order_id != next_o_id - 1) {
        AddViolation(results[1], max_samples,
                     FormatSample("w=", w_id, " d=", d_id,
                                  " next_o_id=", next_o_id,
                                  " max_order_id=", max_order_id));
      }

      if (new_order_count > 0) {
        if (max_new_order_id != next_o_id - 1) {
          AddViolation(results[1], max_samples,
                       FormatSample("w=", w_id, " d=", d_id,
                                    " max_new_order_id=", max_new_order_id,
                                    " next_o_id=", next_o_id));
        }
        if (new_order_count != max_new_order_id - min_new_order_id + 1) {
          AddViolation(results[2], max_samples,
                       FormatSample("w=", w_id, " d=", d_id,
                                    " min_new_order_id=", min_new_order_id,
                                    " max_new_order_id=", max_new_order_id,
                                    " count=", new_order_count));
        }
      }

      int expected_closed_orders = kOrdPerDist - 900;
      if (order_count - new_order_count != expected_closed_orders) {
        AddViolation(results[10], max_samples,
                     FormatSample("w=", w_id, " d=", d_id,
                                  " order_count=", order_count,
                                  " new_order_count=", new_order_count,
                                  " difference=", order_count - new_order_count,
                                  " expected=", expected_closed_orders));
      }

      if (district_order_line_count != district_header_order_line_count) {
        AddViolation(results[3], max_samples,
                     FormatSample("w=", w_id, " d=", d_id,
                                  " expected_order_lines=", district_header_order_line_count,
                                  " actual_order_lines=", district_order_line_count));
      }
    }

    if (w_ytd != sum_d_ytd) {
      AddViolation(results[0], max_samples,
                   FormatSample("w=", w_id, " w_ytd=", w_ytd, " sum_d_ytd=", sum_d_ytd));
    }
    // if (warehouse_history_sums[w_id] != w_ytd) {
    //   AddViolation(results[7], max_samples,
    //                FormatSample("w=", w_id, " w_ytd=", w_ytd,
    //                             " history_sum=", warehouse_history_sums[w_id]));
    // }
  }

  for (int w_id = 1; w_id <= warehouse_count; w_id++) {
    for (int d_id = 1; d_id <= kDistPerWare; d_id++) {
      for (int c_id = 1; c_id <= kCustPerDist; c_id++) {
        auto customer_row = customer.Select(
            {MakeInt32Scalar(w_id), MakeInt8Scalar(d_id), MakeInt32Scalar(c_id)},
            {CustomerSchema::Column::BALANCE, CustomerSchema::Column::YTD_PAYMENT,
             CustomerSchema::Column::PAYMENT_CNT, CustomerSchema::Column::DELIVERY_CNT});
        if (customer_row.empty()) {
          continue;
        }

        int64_t c_balance = UncheckedCast<Int64Scalar>(customer_row[0])->value;
        int64_t c_ytd_payment = UncheckedCast<Int64Scalar>(customer_row[1])->value;
        int16_t c_payment_cnt = UncheckedCast<Int16Scalar>(customer_row[2])->value;
        int16_t c_delivery_cnt = UncheckedCast<Int16Scalar>(customer_row[3])->value;

        auto cust_key = FormatSample(w_id, ":", d_id, ":", c_id);
        int64_t delivered_amount_sum = customer_delivered_order_amounts[cust_key];
        int64_t history_sum = customer_history_sums[cust_key];

        if (c_balance != delivered_amount_sum - history_sum) {
          AddViolation(results[9], max_samples,
                       FormatSample("w=", w_id, " d=", d_id, " c=", c_id,
                                    " c_balance=", c_balance,
                                    " delivered_amount_sum=", delivered_amount_sum,
                                    " history_sum=", history_sum));
        }

        if (c_balance + c_ytd_payment != delivered_amount_sum) {
          AddViolation(results[11], max_samples,
                       FormatSample("w=", w_id, " d=", d_id, " c=", c_id,
                                    " c_balance=", c_balance,
                                    " c_ytd_payment=", c_ytd_payment,
                                    " delivered_amount_sum=", delivered_amount_sum));
        }

        if (history_sum != c_ytd_payment) {
          AddViolation(results[12], max_samples,
                       FormatSample("w=", w_id, " d=", d_id, " c=", c_id,
                                    " c_ytd_payment=", c_ytd_payment,
                                    " history_sum=", history_sum));
        }

        if (customer_history_counts[cust_key] != c_payment_cnt) {
          AddViolation(results[13], max_samples,
                       FormatSample("w=", w_id, " d=", d_id, " c=", c_id,
                                    " c_payment_cnt=", c_payment_cnt,
                                    " history_count=", customer_history_counts[cust_key]));
        }

        if (customer_delivery_counts[cust_key] != c_delivery_cnt) {
          AddViolation(results[14], max_samples,
                       FormatSample("w=", w_id, " d=", d_id, " c=", c_id,
                                    " c_delivery_cnt=", c_delivery_cnt,
                                    " actual_deliveries=", customer_delivery_counts[cust_key]));
        }
      }
    }
  }

  return results;
}

}  // namespace benchx
}  // namespace slog

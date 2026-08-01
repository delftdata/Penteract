#include "execution/benchx/transaction.h"

namespace slog {
namespace benchx {

OrderStatusTxn::OrderStatusTxn(const StorageAdapterPtr& storage_adapter, const std::vector<int>& w_ids,
                               const std::vector<int>& d_ids, const std::vector<int>& c_ids,
                               const std::vector<int>& o_ids, double lines_intensity)
    : order_(storage_adapter), order_line_(storage_adapter), lines_intensity_(lines_intensity) {
  for (auto w_id : w_ids) {
    a_w_ids_.push_back(MakeInt32Scalar(w_id));
  }
  for (auto d_id : d_ids) {
    a_d_ids_.push_back(MakeInt8Scalar(d_id));
  }
  for (auto c_id : c_ids) {
    a_c_ids_.push_back(MakeInt32Scalar(c_id));
  }
  for (auto o_id : o_ids) {
    a_o_ids_.push_back(MakeInt32Scalar(o_id));
  }

  size_t n = w_ids.size();
  o_entry_dates_.resize(n);
  o_carrier_ids_.resize(n);
  o_ol_cnts_.resize(n);
  ol_i_ids_.resize(n);
  ol_supplies_.resize(n);
  ol_quantities_.resize(n);
  ol_amounts_.resize(n);
  ol_delivery_dates_.resize(n);
}

bool OrderStatusTxn::Read() {
  bool ok = true;
  size_t n = a_w_ids_.size();
  // LOG(INFO) << "OrderStatusTxn::Read started for " << n << " orders";

  for (size_t i = 0; i < n; ++i) {
    auto order_res =
        order_.Select({a_w_ids_[i], a_d_ids_[i], a_o_ids_[i]},
                      {OrderSchema::Column::ENTRY_D, OrderSchema::Column::CARRIER_ID, OrderSchema::Column::OL_CNT});
    // LOG(INFO) << "Fetching order details for index " << i << " with W_ID=" << a_w_ids_[i]->to_string()
    //           << " D_ID=" << a_d_ids_[i]->to_string()
    //           << " O_ID=" << a_o_ids_[i]->to_string();
    int ol_cnt = 15;  // Max possible lines to lock during dry-run
    if (order_res.size() >= 3) {
      o_entry_dates_[i] = std::static_pointer_cast<Int64Scalar>(order_res[0]);
      o_carrier_ids_[i] = std::static_pointer_cast<Int8Scalar>(order_res[1]);
      o_ol_cnts_[i] = std::static_pointer_cast<Int8Scalar>(order_res[2]);
      ol_cnt = o_ol_cnts_[i]->value;
      if (lines_intensity_ > 0.0 && lines_intensity_ < 1.0) {
        ol_cnt = std::max(1, static_cast<int>(ol_cnt * lines_intensity_));
      }
    } else {
      SetError("Order does not exist for index " + std::to_string(i));
      ok = false;
    }

    for (int j = 1; j <= ol_cnt; ++j) {
      // LOG(INFO) << "Fetching order line details for index " << i << " line " << j
      //           << " with W_ID=" << a_w_ids_[i]->to_string()
      //           << " D_ID=" << a_d_ids_[i]->to_string()
      //           << " O_ID=" << a_o_ids_[i]->to_string()
      //           << " LINE_NUM=" << j;
      auto ol_res = order_line_.Select(
          {a_w_ids_[i], a_d_ids_[i], a_o_ids_[i], MakeInt8Scalar(j)},
          {OrderLineSchema::Column::I_ID, OrderLineSchema::Column::SUPPLY_W_ID, OrderLineSchema::Column::QUANTITY,
           OrderLineSchema::Column::AMOUNT, OrderLineSchema::Column::DELIVERY_D});
      if (ol_res.size() < 5) {
        SetError("Order line does not exist for index " + std::to_string(i) + " line " + std::to_string(j));
        ok = false;
        continue;
      }

      ol_i_ids_[i].push_back(std::static_pointer_cast<Int32Scalar>(ol_res[0]));
      ol_supplies_[i].push_back(std::static_pointer_cast<Int32Scalar>(ol_res[1]));
      ol_quantities_[i].push_back(std::static_pointer_cast<Int8Scalar>(ol_res[2]));
      ol_amounts_[i].push_back(std::static_pointer_cast<Int32Scalar>(ol_res[3]));
      ol_delivery_dates_[i].push_back(std::static_pointer_cast<Int64Scalar>(ol_res[4]));
    }
  }
  // LOG(INFO) << "OrderStatusTxn::Read completed for " << n << " orders";

  return ok;
}

void OrderStatusTxn::Compute() {
  // Nothing to compute
}

bool OrderStatusTxn::Write() {
  // Order status is read-only
  return true;
}

}  // namespace benchx
}  // namespace slog
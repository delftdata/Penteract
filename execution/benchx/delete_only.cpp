#include "execution/benchx/transaction.h"

namespace slog {
namespace benchx {

DeleteTxn::DeleteTxn(const StorageAdapterPtr& storage_adapter,
                     const std::vector<int>& w_ids, const std::vector<int>& d_ids,
                     const std::vector<int>& c_w_ids, const std::vector<int>& c_d_ids,
                     const std::vector<int>& c_ids, const std::vector<int>& h_ids)
    : warehouse_(storage_adapter), district_(storage_adapter), customer_(storage_adapter), history_(storage_adapter) {
  for (auto w_id : w_ids) {
    a_w_ids_.push_back(MakeInt32Scalar(w_id));
  }
  for (auto d_id : d_ids) {
    a_d_ids_.push_back(MakeInt8Scalar(d_id));
  }
  for (auto c_w_id : c_w_ids) {
    a_c_w_ids_.push_back(MakeInt32Scalar(c_w_id));
  }
  for (auto c_d_id : c_d_ids) {
    a_c_d_ids_.push_back(MakeInt8Scalar(c_d_id));
  }
  for (auto c_id : c_ids) {
    a_c_ids_.push_back(MakeInt32Scalar(c_id));
  }
  for (auto h_id : h_ids) {
    a_h_ids_.push_back(MakeInt32Scalar(h_id));
  }
}

bool DeleteTxn::Read() {
  bool ok = true;
  size_t n = a_w_ids_.size();

  for (size_t i = 0; i < n; ++i) {
    if (auto res = warehouse_.Select({a_w_ids_[i]}, {WarehouseSchema::Column::NAME});
        res.empty()) {
      SetError("Warehouse does not exist for index " + std::to_string(i));
      ok = false;
    }

    if (auto res = district_.Select({a_w_ids_[i], a_d_ids_[i]}, {DistrictSchema::Column::NAME});
        res.empty()) {
      SetError("District does not exist for index " + std::to_string(i));
      ok = false;
    }

    if (auto res = customer_.Select({a_c_w_ids_[i], a_c_d_ids_[i], a_c_ids_[i]}, {CustomerSchema::Column::FULL_NAME});
        res.empty()) {
      SetError("Customer does not exist for index " + std::to_string(i));
      ok = false;
    }

    // Read the history row before deleting it
    if (auto res = history_.Select({a_w_ids_[i], a_d_ids_[i], a_c_ids_[i], a_h_ids_[i]}, {HistorySchema::Column::DATA});
        res.empty()) {
      SetError("History does not exist for index " + std::to_string(i));
      ok = false;
    }
  }

  return ok;
}

void DeleteTxn::Compute() {
  // Nothing really needed, but keep symmetry
}

bool DeleteTxn::Write() {
  //LOG(INFO) << "DeleteTxn::Write started for " << a_w_ids_.size() << " deletes";
  size_t n = a_w_ids_.size();
  for (size_t i = 0; i < n; ++i) {
    //LOG(INFO) << "Deleting history row for index " << i
    //          << ": Warehouse ID: " << a_w_ids_[i]->value 
    //          << ", District ID: " << static_cast<int>(a_d_ids_[i]->value)
    //          << ", Customer ID: " << a_c_ids_[i]->value
    //          << ", History ID: " << a_h_ids_[i]->value;
    if (!history_.Delete({a_w_ids_[i], a_d_ids_[i], a_c_ids_[i], a_h_ids_[i]})) {
      SetError("Failed to delete history row for index " + std::to_string(i));
      return false;
    }
  }
  //LOG(INFO) << "DeleteTxn::Write finished for " << a_w_ids_.size() << " deletes";
  return true;
}

}  // namespace benchx
}  // namespace slog
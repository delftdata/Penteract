#include "execution/benchx/transaction.h"

namespace slog {
namespace benchx {

StockLevelTxn::StockLevelTxn(const StorageAdapterPtr& storage_adapter, const std::vector<int>& w_ids, const std::vector<int>& d_ids, const std::vector<int>& o_ids,
                             const std::array<int, kTotalItems>& i_ids)
    : district_(storage_adapter), order_line_(storage_adapter), stock_(storage_adapter) {
  for (int w_id : w_ids) {
    a_w_ids_.push_back(MakeInt32Scalar(w_id));
  }
  for (int d_id : d_ids) {
    a_d_ids_.push_back(MakeInt8Scalar(d_id));
  }
  for (int o_id : o_ids) {
    a_o_ids_.push_back(MakeInt32Scalar(o_id));
  }
  for (int i = 0; i < kTotalItems; i++) {
    a_i_ids_[i] = MakeInt32Scalar(i_ids[i]);
  }
}

bool StockLevelTxn::Read() {
  auto o_id = MakeInt32Scalar();
  auto ol_number = MakeInt8Scalar();

  for (size_t k = 0; k < a_w_ids_.size(); k++) {
    district_.Select({a_w_ids_[k], a_d_ids_[k]}, {DistrictSchema::Column::NEXT_O_ID});
    for (int i = a_o_ids_[k]->value - 20; i < a_o_ids_[k]->value; i++) {
      o_id->value = i;
      for (int j = 0; j < kLinePerOrder; j++) {
        ol_number->value = j;
        order_line_.Select({a_w_ids_[k], a_d_ids_[k], o_id, ol_number}, {OrderLineSchema::Column::I_ID});
      }
    }
    for (int i = 0; i < kTotalItems; i++) {
      stock_.Select({a_w_ids_[k], a_i_ids_[i]}, {StockSchema::Column::QUANTITY});
    }
  }
  return true;
}

}  // namespace benchx
}  // namespace slog
#include "execution/benchx/transaction.h"

namespace slog {
namespace benchx {

GetItemByNameTxn::GetItemByNameTxn(const StorageAdapterPtr& storage_adapter, const std::vector<int>& w_ids,
                                   const std::vector<std::string>& i_names)
    : item_by_name_(storage_adapter) {
  for (auto w_id : w_ids) {
    a_w_ids_.push_back(MakeInt32Scalar(w_id));
  }
  for (const auto& name : i_names) {
    a_i_names_.push_back(MakeFixedTextScalar<24>(name));
  }
  i_ids_.resize(w_ids.size());
  for (size_t i = 0; i < i_ids_.size(); i++) {
    i_ids_[i] = MakeInt32Scalar();
  }
}

bool GetItemByNameTxn::Read() {
  bool ok = true;
  for (size_t i = 0; i < i_ids_.size(); i++) {
    auto res = item_by_name_.Select({a_w_ids_[i], a_i_names_[i]}, {ItemByNameSchema::Column::ID});
    if (res.empty()) {
      SetError("Item does not exist for the given name");
      ok = false;
    } else {
      i_ids_[i] = std::static_pointer_cast<Int32Scalar>(res[0]);
    }
  }
  return ok;
}

void GetItemByNameTxn::Compute() {}

bool GetItemByNameTxn::Write() {
  return true;
}

}  // namespace benchx
}  // namespace slog

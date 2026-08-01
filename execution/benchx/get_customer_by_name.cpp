#include "execution/benchx/transaction.h"

namespace slog {
namespace benchx {

GetCustomerByNameTxn::GetCustomerByNameTxn(const StorageAdapterPtr& storage_adapter, const std::vector<int>& w_ids,
                                           const std::vector<int>& d_ids, const std::vector<std::string>& c_names)
    : customer_by_name_(storage_adapter) {
  for (auto w_id : w_ids) {
    a_w_ids_.push_back(MakeInt32Scalar(w_id));
  }
  for (auto d_id : d_ids) {
    a_d_ids_.push_back(MakeInt8Scalar(d_id));
  }
  for (const auto& name : c_names) {
    a_c_names_.push_back(MakeFixedTextScalar<34>(name));
  }
  c_ids_.resize(w_ids.size());
  for (size_t i = 0; i < c_ids_.size(); i++) {
    c_ids_[i] = MakeInt32Scalar();
  }
}

bool GetCustomerByNameTxn::Read() {
  bool ok = true;
  for (size_t i = 0; i < c_ids_.size(); i++) {
    auto res = customer_by_name_.Select({a_w_ids_[i], a_d_ids_[i], a_c_names_[i]}, {CustomerByNameSchema::Column::ID});
    if (res.empty()) {
      SetError("Customer does not exist for the given name");
      ok = false;
    } else {
      c_ids_[i] = std::static_pointer_cast<Int32Scalar>(res[0]);
    }
  }
  return ok;
}

void GetCustomerByNameTxn::Compute() {}

bool GetCustomerByNameTxn::Write() {
  return true;
}

}  // namespace benchx
}  // namespace slog

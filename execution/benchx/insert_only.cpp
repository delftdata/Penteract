#include "execution/benchx/transaction.h"

namespace slog {
namespace benchx {

// For reference:
// Code value	   Meaning	                         TPC-C equivalent
// a_w_id_	     Warehouse receiving payment	     H_W_ID
// a_d_id_	     District receiving payment	       H_D_ID
// a_c_id_	     Customer                          ID	H_C_ID
// a_h_id_	     Artificial history ID	           ❌ not in spec
// a_c_d_id_   	 Customer district                 H_C_D_ID
// a_c_w_id_	   Customer warehouse                H_C_W_ID
// datetime_	   Timestamp	                       H_DATE
// a_amount_	   Payment amount	                   H_AMOUNT
// new_h_data_	 Text (W name + D name)            H_DATA

InsertTxn::InsertTxn(const StorageAdapterPtr& storage_adapter, 
                     const std::vector<int>& w_ids, const std::vector<int>& d_ids, 
                     const std::vector<int>& c_w_ids, const std::vector<int>& c_d_ids, 
                     const std::vector<int>& c_ids, const std::vector<int64_t>& amounts, 
                     const std::vector<int64_t>& datetimes, const std::vector<int>& h_ids)
    : warehouse_(storage_adapter), district_(storage_adapter), customer_(storage_adapter), history_(storage_adapter) {
  size_t n = w_ids.size();
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
  for (auto amount : amounts) {
    a_amounts_.push_back(MakeInt32Scalar(static_cast<int32_t>(amount)));
  }
  for (auto datetime : datetimes) {
    datetimes_.push_back(MakeInt64Scalar(datetime));
  }
  for (auto h_id : h_ids) {
    a_h_ids_.push_back(MakeInt32Scalar(h_id));
  }
  new_h_datas_.resize(n);
  for (auto& nhd : new_h_datas_) {
    nhd = MakeFixedTextScalar();
  }
}

bool InsertTxn::Read() {
  //LOG(INFO) << "InsertTxn::Read started for " << a_w_ids_.size() << " inserts";
  bool ok = true;
  size_t n = a_w_ids_.size();
  w_names_.resize(n);
  w_addresses_.resize(n);
  w_ytds_.resize(n);
  d_names_.resize(n);
  d_addresses_.resize(n);
  d_ytds_.resize(n);
  c_full_names_.resize(n);
  c_addresses_.resize(n);
  c_phones_.resize(n);
  c_sinces_.resize(n);
  c_credits_.resize(n);
  c_credit_lims_.resize(n);
  c_discounts_.resize(n);
  c_balances_.resize(n);
  c_ytd_payments_.resize(n);
  c_payment_cnts_.resize(n);
  c_datas_.resize(n);
  //LOG(INFO) << "InsertTxn::Read initialized result vectors";

  for (size_t i = 0; i < n; ++i) {
    if (auto res = warehouse_.Select(
            {a_w_ids_[i]}, {WarehouseSchema::Column::NAME, WarehouseSchema::Column::ADDRESS, WarehouseSchema::Column::YTD});
        !res.empty()) {
      w_names_[i] = UncheckedCast<FixedTextScalar>(res[0]);
      w_addresses_[i] = UncheckedCast<FixedTextScalar>(res[1]);
      w_ytds_[i] = UncheckedCast<Int64Scalar>(res[2]);
    } else {
      //LOG(ERROR) << "Warehouse not found: w=" << a_w_ids_[i]->value << " for index " << i;
      SetError("Warehouse does not exist for index " + std::to_string(i));
      ok = false;
    }
    //LOG(INFO) << "InsertTxn::Read read warehouse for index " << i;

    if (auto res = district_.Select({a_w_ids_[i], a_d_ids_[i]}, {DistrictSchema::Column::NAME, DistrictSchema::Column::ADDRESS,
                                                                 DistrictSchema::Column::YTD});
        !res.empty()) {
      d_names_[i] = UncheckedCast<FixedTextScalar>(res[0]);
      d_addresses_[i] = UncheckedCast<FixedTextScalar>(res[1]);
      d_ytds_[i] = UncheckedCast<Int64Scalar>(res[2]);
    } else {
      //LOG(ERROR) << "District does not exist for index " + std::to_string(i) << ": w=" << a_w_ids_[i]->value << ", d=" << static_cast<int>(a_d_ids_[i]->value);
      SetError("District does not exist for index " + std::to_string(i));
      ok = false;
    }
    //LOG(INFO) << "InsertTxn::Read read district for index " << i;
    if (auto res = customer_.Select(
            {a_c_w_ids_[i], a_c_d_ids_[i], a_c_ids_[i]},
            {CustomerSchema::Column::FULL_NAME, CustomerSchema::Column::ADDRESS, CustomerSchema::Column::PHONE,
             CustomerSchema::Column::SINCE, CustomerSchema::Column::CREDIT, CustomerSchema::Column::CREDIT_LIM,
             CustomerSchema::Column::DISCOUNT, CustomerSchema::Column::BALANCE, CustomerSchema::Column::YTD_PAYMENT,
             CustomerSchema::Column::PAYMENT_CNT, CustomerSchema::Column::DATA});
        !res.empty()) {
      c_full_names_[i] = UncheckedCast<FixedTextScalar>(res[0]);
      c_addresses_[i] = UncheckedCast<FixedTextScalar>(res[1]);
      c_phones_[i] = UncheckedCast<FixedTextScalar>(res[2]);
      c_sinces_[i] = UncheckedCast<Int64Scalar>(res[3]);
      c_credits_[i] = UncheckedCast<FixedTextScalar>(res[4]);
      c_credit_lims_[i] = UncheckedCast<Int64Scalar>(res[5]);
      c_discounts_[i] = UncheckedCast<Int32Scalar>(res[6]);
      c_balances_[i] = UncheckedCast<Int64Scalar>(res[7]);
      c_ytd_payments_[i] = UncheckedCast<Int64Scalar>(res[8]);
      c_payment_cnts_[i] = UncheckedCast<Int16Scalar>(res[9]);
      c_datas_[i] = UncheckedCast<FixedTextScalar>(res[10]);
    } else {
      //LOG(ERROR) << "Customer does not exist for index " + std::to_string(i) << ": w=" << a_w_ids_[i]->value << ", d=" << static_cast<int>(a_d_ids_[i]->value) << ", c=" << a_c_ids_[i]->value;
      SetError("Customer does not exist for index " + std::to_string(i));
      ok = false;
    }
    //LOG(INFO) << "InsertTxn::Read read customer for index " << i;

  }
  return ok;
}

void InsertTxn::Compute() {
  //LOG(INFO) << "InsertTxn::Compute started for " << a_w_ids_.size() << " inserts";
  size_t n = a_w_ids_.size();
  for (size_t i = 0; i < n; ++i) {
    new_h_datas_[i]->buffer = w_names_[i]->buffer + "    " + d_names_[i]->buffer;
  }
  //LOG(INFO) << "InsertTxn::Compute finished for " << a_w_ids_.size() << " inserts";
}

bool InsertTxn::Write() {
  //LOG(INFO) << "InsertTxn::Write started for " << a_w_ids_.size() << " inserts";
  size_t n = a_w_ids_.size();
  for (size_t i = 0; i < n; ++i) {
    history_.Insert({a_w_ids_[i], a_d_ids_[i], a_c_ids_[i], a_h_ids_[i], a_c_d_ids_[i], a_c_w_ids_[i], datetimes_[i], a_amounts_[i], new_h_datas_[i]});
  }
  //LOG(INFO) << "InsertTxn::Write finished for " << a_w_ids_.size() << " inserts";
  return true;
}

}  // namespace benchx
}  // namespace slog
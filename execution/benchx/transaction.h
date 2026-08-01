#pragma once

#include <array>

#include "execution/benchx/constants.h"
#include "execution/benchx/table.h"

namespace slog {
namespace benchx {

struct OrderLine {
  int id;
  int supply_w_id;
  int item_id;
  int quantity;
};

struct OrderLineScalar {
  Int8ScalarPtr a_id;
  Int32ScalarPtr a_supply_w_id;
  Int32ScalarPtr a_item_id;
  Int8ScalarPtr a_quantity;
  Int32ScalarPtr i_price;
  Int16ScalarPtr s_quantity;
  Int32ScalarPtr amount;
  FixedTextScalarPtr dist_info;
};

class BenchXTransaction {
 public:
  virtual ~BenchXTransaction() = default;
  bool Execute() {
    if (!Read()) {
      return false;
    }
    Compute();
    if (!Write()) {
      return false;
    }
    return true;
  }
  virtual bool Read() = 0;
  virtual void Compute() = 0;
  virtual bool Write() = 0;

  const std::string& error() const { return error_; }

 protected:
  void SetError(const std::string& error) {
    if (error_.empty()) error_ = error;
  }

 private:
  std::string error_;
};

class NewOrderTxn : public BenchXTransaction {
 public:
  NewOrderTxn(const StorageAdapterPtr& storage_adapter, int w_id, int d_id, int c_id, int o_id, int64_t datetime,
              int i_w_id, const std::vector<OrderLine>& ol);

  bool Read() final;
  void Compute() final;
  bool Write() final;

 private:
  Table<WarehouseSchema> warehouse_;
  Table<DistrictSchema> district_;
  Table<CustomerSchema> customer_;
  Table<NewOrderSchema> new_order_;
  Table<OrderSchema> order_;
  Table<OrderLineSchema> order_line_;
  Table<ItemSchema> item_;
  Table<StockSchema> stock_;

  Int32ScalarPtr a_w_id_;
  Int8ScalarPtr a_d_id_;
  Int32ScalarPtr a_c_id_;
  Int32ScalarPtr a_o_id_;
  Int64ScalarPtr datetime_;
  Int32ScalarPtr i_w_id_;
  std::vector<OrderLineScalar> a_ol_;

  // Read results
  Int32ScalarPtr w_tax_;
  Int32ScalarPtr c_discount_;
  FixedTextScalarPtr c_last_;
  FixedTextScalarPtr c_credit_;
  Int32ScalarPtr d_tax_;
  Int32ScalarPtr d_next_o_id_;
  Int32ScalarPtr new_d_next_o_id_;
  Int8ScalarPtr all_local_;
  size_t ol_cnt_;  // actual number of order lines
};

class InsertTxn : public BenchXTransaction {
 public:
  InsertTxn(const StorageAdapterPtr& storage_adapter, const std::vector<int>& w_ids, const std::vector<int>& d_ids,
            const std::vector<int>& c_w_ids, const std::vector<int>& c_d_ids, const std::vector<int>& c_ids,
            const std::vector<int64_t>& amounts, const std::vector<int64_t>& datetimes, const std::vector<int>& h_ids);
  bool Read() final;
  void Compute() final;
  bool Write() final;

 private:
  Table<WarehouseSchema> warehouse_;
  Table<DistrictSchema> district_;
  Table<CustomerSchema> customer_;
  Table<HistorySchema> history_;

  // Arguments
  std::vector<Int32ScalarPtr> a_w_ids_;
  std::vector<Int8ScalarPtr> a_d_ids_;
  std::vector<Int32ScalarPtr> a_c_w_ids_;
  std::vector<Int8ScalarPtr> a_c_d_ids_;
  std::vector<Int32ScalarPtr> a_c_ids_;
  std::vector<Int32ScalarPtr> a_amounts_;
  std::vector<Int64ScalarPtr> datetimes_;
  std::vector<Int32ScalarPtr> a_h_ids_;

  // Read results
  std::vector<FixedTextScalarPtr> w_names_;
  std::vector<FixedTextScalarPtr> w_addresses_;
  std::vector<Int64ScalarPtr> w_ytds_;
  std::vector<FixedTextScalarPtr> d_names_;
  std::vector<FixedTextScalarPtr> d_addresses_;
  std::vector<Int64ScalarPtr> d_ytds_;
  std::vector<FixedTextScalarPtr> c_full_names_;
  std::vector<FixedTextScalarPtr> c_addresses_;
  std::vector<FixedTextScalarPtr> c_phones_;
  std::vector<Int64ScalarPtr> c_sinces_;
  std::vector<FixedTextScalarPtr> c_credits_;
  std::vector<Int64ScalarPtr> c_credit_lims_;
  std::vector<Int32ScalarPtr> c_discounts_;
  std::vector<Int64ScalarPtr> c_balances_;
  std::vector<Int64ScalarPtr> c_ytd_payments_;
  std::vector<Int16ScalarPtr> c_payment_cnts_;
  std::vector<FixedTextScalarPtr> c_datas_;

  // Computed values
  std::vector<Int64ScalarPtr> new_w_ytds_;
  std::vector<Int64ScalarPtr> new_d_ytds_;
  std::vector<Int64ScalarPtr> new_c_balances_;
  std::vector<Int64ScalarPtr> new_c_ytd_payments_;
  std::vector<Int16ScalarPtr> new_c_payment_cnts_;
  std::vector<FixedTextScalarPtr> new_h_datas_;
};

class GetCustomerByNameTxn : public BenchXTransaction {
 public:
  GetCustomerByNameTxn(const StorageAdapterPtr& storage_adapter, const std::vector<int>& w_ids,
                       const std::vector<int>& d_ids, const std::vector<std::string>& c_names);
  bool Read() final;
  void Compute() final;
  bool Write() final;

 private:
  Table<CustomerByNameSchema> customer_by_name_;
  std::vector<Int32ScalarPtr> a_w_ids_;
  std::vector<Int8ScalarPtr> a_d_ids_;
  std::vector<FixedTextScalarPtr> a_c_names_;
  std::vector<Int32ScalarPtr> c_ids_;
};

class GetItemByNameTxn : public BenchXTransaction {
 public:
  GetItemByNameTxn(const StorageAdapterPtr& storage_adapter, const std::vector<int>& w_ids,
                   const std::vector<std::string>& i_names);
  bool Read() final;
  void Compute() final;
  bool Write() final;

 private:
  Table<ItemByNameSchema> item_by_name_;
  std::vector<Int32ScalarPtr> a_w_ids_;
  std::vector<FixedTextScalarPtr> a_i_names_;
  std::vector<Int32ScalarPtr> i_ids_;
};

class OrderStatusTxn : public BenchXTransaction {
 public:
  OrderStatusTxn(const StorageAdapterPtr& storage_adapter, const std::vector<int>& w_ids, const std::vector<int>& d_ids,
                 const std::vector<int>& c_ids, const std::vector<int>& o_ids, double lines_intensity = 1.0);
  bool Read() final;
  void Compute() final;
  bool Write() final;

 private:
  Table<OrderSchema> order_;
  Table<OrderLineSchema> order_line_;
  double lines_intensity_;

  std::vector<Int32ScalarPtr> a_w_ids_;
  std::vector<Int8ScalarPtr> a_d_ids_;
  std::vector<Int32ScalarPtr> a_c_ids_;
  std::vector<Int32ScalarPtr> a_o_ids_;

  // Read results
  std::vector<Int64ScalarPtr> o_entry_dates_;
  std::vector<Int8ScalarPtr> o_carrier_ids_;
  std::vector<Int8ScalarPtr> o_ol_cnts_;
  std::vector<std::vector<Int32ScalarPtr>> ol_i_ids_;
  std::vector<std::vector<Int32ScalarPtr>> ol_supplies_;
  std::vector<std::vector<Int8ScalarPtr>> ol_quantities_;
  std::vector<std::vector<Int32ScalarPtr>> ol_amounts_;
  std::vector<std::vector<Int64ScalarPtr>> ol_delivery_dates_;
};

class DeleteTxn : public BenchXTransaction {
 public:
  DeleteTxn(const StorageAdapterPtr& storage_adapter, const std::vector<int>& w_ids, const std::vector<int>& d_ids,
            const std::vector<int>& c_w_ids, const std::vector<int>& c_d_ids, const std::vector<int>& c_ids,
            const std::vector<int>& h_ids);
  bool Read() final;
  void Compute() final;
  bool Write() final;

 private:
  Table<CustomerSchema> customer_;
  Table<DistrictSchema> district_;
  Table<WarehouseSchema> warehouse_;
  Table<HistorySchema> history_;

  std::vector<Int32ScalarPtr> a_w_ids_;
  std::vector<Int8ScalarPtr> a_d_ids_;
  std::vector<Int32ScalarPtr> a_c_w_ids_;
  std::vector<Int8ScalarPtr> a_c_d_ids_;
  std::vector<Int32ScalarPtr> a_c_ids_;
  std::vector<Int32ScalarPtr> a_h_ids_;
};

class StockLevelTxn : public BenchXTransaction {
 public:
  constexpr static int kTotalItems = 200;
  StockLevelTxn(const StorageAdapterPtr& storage_adapter, const std::vector<int>& w_ids, const std::vector<int>& d_ids, const std::vector<int>& o_ids,
                const std::array<int, kTotalItems>& i_ids);
  bool Read() final;
  void Compute() final {}
  bool Write() final { return true; }

 private:
  Table<DistrictSchema> district_;
  Table<OrderLineSchema> order_line_;
  Table<StockSchema> stock_;

  // Arguments
  std::vector<Int32ScalarPtr> a_w_ids_;
  std::vector<Int8ScalarPtr> a_d_ids_;
  std::vector<Int32ScalarPtr> a_o_ids_;
  std::array<Int32ScalarPtr, kTotalItems> a_i_ids_;
};

}  // namespace benchx
}  // namespace slog
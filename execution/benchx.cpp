#include <string>
#include <vector>
#include <sstream>

#include "execution/execution.h"
#include "execution/benchx/constants.h"
#include "execution/benchx/transaction.h"

namespace slog {

using std::stoi;
using std::stoll;

// Helper function to split a string by comma and convert to vector of int
std::vector<int> split_and_stoi(const std::string& s) {
  std::vector<int> result;
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, ',')) {
    result.push_back(stoi(item));
  }
  return result;
}

// Helper function to split a string by comma and convert to vector of int64_t
std::vector<int64_t> split_and_stoll(const std::string& s) {
  std::vector<int64_t> result;
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, ',')) {
    result.push_back(stoll(item));
  }
  return result;
}

BenchXExecution::BenchXExecution(const SharderPtr& sharder, const std::shared_ptr<Storage>& storage)
    : sharder_(sharder), storage_(storage) {}

void BenchXExecution::Execute(Transaction& txn) {
  auto txn_adapter = std::make_shared<benchx::TxnStorageAdapter>(txn);

  if (txn.code().procedures().empty() || txn.code().procedures(0).args().empty()) {
    txn.set_status(TransactionStatus::ABORTED);
    txn.set_abort_reason("Invalid code");
    return;
  }

  std::ostringstream abort_reason;
  const auto& args = txn.code().procedures(0).args();
  const auto& txn_name = args[0];

  if (txn_name == "new_order") {
    if (args.size() < 7 || args.size() > 8) {
      txn.set_status(TransactionStatus::ABORTED);
      txn.set_abort_reason("NewOrder Txn - Invalid number of arguments");
      return;
    }
    int w_id = stoi(args[1]);
    int d_id = stoi(args[2]);
    int c_id = stoi(args[3]);
    int o_id = stoi(args[4]);
    int64_t datetime = stoll(args[5]);
    int w_i_id = stoi(args[6]);

    std::vector<benchx::OrderLine> ol;
    int num_lines = txn.code().procedures_size() - 1;
    for (int i = 0; i < num_lines; i++) {
      const auto& order_line = txn.code().procedures(i + 1);
      if (order_line.args_size() != 4) {
        txn.set_status(TransactionStatus::ABORTED);
        txn.set_abort_reason("NewOrder Txn - Invalid number of arguments for order line");
        return;
      }
      int ol_id = stoi(order_line.args(0));
      int supply_w_id = stoi(order_line.args(1));
      int item_id = stoi(order_line.args(2));
      int quantity = stoi(order_line.args(3));
      ol.push_back(benchx::OrderLine{
          .id = ol_id, .supply_w_id = supply_w_id, .item_id = item_id, .quantity = quantity});
    }

    benchx::NewOrderTxn new_order(txn_adapter, w_id, d_id, c_id, o_id, datetime, w_i_id, ol);
    if (!new_order.Execute()) {
      txn.set_status(TransactionStatus::ABORTED);
      txn.set_abort_reason("NewOrder Txn - " + new_order.error());
      return;
    }
  } else if (txn_name == "insert_only") {
    if (args.size() < 9 || args.size() > 10) {
      txn.set_status(TransactionStatus::ABORTED);
      txn.set_abort_reason("InsertOnly Txn - Invalid number of arguments");
      return;
    }
    // Parse comma-separated strings into vectors
    std::vector<int> w_ids = split_and_stoi(args[1]);
    std::vector<int> d_ids = split_and_stoi(args[2]);
    std::vector<int> c_w_ids = split_and_stoi(args[3]);
    std::vector<int> c_d_ids = split_and_stoi(args[4]);
    std::vector<int> c_ids = split_and_stoi(args[5]);
    std::vector<int64_t> amounts = split_and_stoll(args[6]);
    std::vector<int64_t> datetimes = split_and_stoll(args[7]);
    std::vector<int> h_ids = split_and_stoi(args[8]);

    // Check if all vectors have the same size
    size_t n = w_ids.size();
    if (d_ids.size() != n || c_w_ids.size() != n || c_d_ids.size() != n || c_ids.size() != n ||
        amounts.size() != n || datetimes.size() != n || h_ids.size() != n) {
      txn.set_status(TransactionStatus::ABORTED);
      txn.set_abort_reason("InsertOnly Txn - Vector sizes do not match");
      return;
    }

    benchx::InsertTxn insert_only(txn_adapter, w_ids, d_ids, c_w_ids, c_d_ids, c_ids, amounts, datetimes, h_ids);
    if (!insert_only.Execute()) {
      txn.set_status(TransactionStatus::ABORTED);
      txn.set_abort_reason("InsertOnly Txn - " + insert_only.error());
      return;
    }
  } else if (txn_name == "order_status") {
    if (args.size() < 5 || args.size() > 6) {
      txn.set_status(TransactionStatus::ABORTED);
      txn.set_abort_reason("OrderStatus Txn - Invalid number of arguments");
      return;
    }

    std::vector<int> w_ids = split_and_stoi(args[1]);
    std::vector<int> d_ids = split_and_stoi(args[2]);
    std::vector<int> c_ids = split_and_stoi(args[3]);
    std::vector<int> o_ids = split_and_stoi(args[4]);

    size_t n = w_ids.size();
    if (d_ids.size() != n || c_ids.size() != n || o_ids.size() != n) {
      txn.set_status(TransactionStatus::ABORTED);
      txn.set_abort_reason("OrderStatus Txn - Vector sizes do not match");
      return;
    }

    benchx::OrderStatusTxn order_status(txn_adapter, w_ids, d_ids, c_ids, o_ids);
    if (!order_status.Execute()) {
      txn.set_status(TransactionStatus::ABORTED);
      txn.set_abort_reason("OrderStatus Txn - " + order_status.error());
      return;
    }
  } else if (txn_name == "get_customer_by_name") {
    if (args.size() < 4 || args.size() > 5) {
      txn.set_status(TransactionStatus::ABORTED);
      txn.set_abort_reason("GetCustomerByName Txn - Invalid number of arguments");
      return;
    }

    std::vector<int> w_ids = split_and_stoi(args[1]);
    std::vector<int> d_ids = split_and_stoi(args[2]);
    
    std::vector<std::string> c_names;
    std::stringstream ss(args[3]);
    std::string item;
    while (std::getline(ss, item, ',')) {
      c_names.push_back(item);
    }

    size_t n = w_ids.size();
    if (d_ids.size() != n || c_names.size() != n) {
      txn.set_status(TransactionStatus::ABORTED);
      txn.set_abort_reason("GetCustomerByName Txn - Vector sizes do not match");
      return;
    }

    benchx::GetCustomerByNameTxn get_customer(txn_adapter, w_ids, d_ids, c_names);
    if (!get_customer.Execute()) {
      txn.set_status(TransactionStatus::ABORTED);
      txn.set_abort_reason("GetCustomerByName Txn - " + get_customer.error());
      return;
    }
  } else if (txn_name == "get_item_by_name") {
    if (args.size() < 3 || args.size() > 4) {
      txn.set_status(TransactionStatus::ABORTED);
      txn.set_abort_reason("GetItemByName Txn - Invalid number of arguments");
      return;
    }

    std::vector<int> w_ids = split_and_stoi(args[1]);
    
    std::vector<std::string> i_names;
    std::stringstream ss(args[2]);
    std::string item;
    while (std::getline(ss, item, ',')) {
      i_names.push_back(item);
    }

    size_t n = w_ids.size();
    if (i_names.size() != n) {
      txn.set_status(TransactionStatus::ABORTED);
      txn.set_abort_reason("GetItemByName Txn - Vector sizes do not match");
      return;
    }

    benchx::GetItemByNameTxn get_item(txn_adapter, w_ids, i_names);
    if (!get_item.Execute()) {
      txn.set_status(TransactionStatus::ABORTED);
      txn.set_abort_reason("GetItemByName Txn - " + get_item.error());
      return;
    }
  } else if (txn_name == "delete_only") {
    if (args.size() < 7 || args.size() > 8) {
      txn.set_status(TransactionStatus::ABORTED);
      txn.set_abort_reason("DeleteOnly Txn - Invalid number of arguments");
      return;
    }

    std::vector<int> w_ids = split_and_stoi(args[1]);
    std::vector<int> d_ids = split_and_stoi(args[2]);
    std::vector<int> c_w_ids = split_and_stoi(args[3]);
    std::vector<int> c_d_ids = split_and_stoi(args[4]);
    std::vector<int> c_ids = split_and_stoi(args[5]);
    std::vector<int> h_ids = split_and_stoi(args[6]);

    size_t n = w_ids.size();
    if (d_ids.size() != n || c_w_ids.size() != n || c_d_ids.size() != n ||
        c_ids.size() != n || h_ids.size() != n) {
      txn.set_status(TransactionStatus::ABORTED);
      txn.set_abort_reason("DeleteOnly Txn - Vector sizes do not match");
      return;
    }

    benchx::DeleteTxn delete_only(txn_adapter, w_ids, d_ids, c_w_ids, c_d_ids, c_ids, h_ids);
    if (!delete_only.Execute()) {
      txn.set_status(TransactionStatus::ABORTED);
      txn.set_abort_reason("DeleteOnly Txn - " + delete_only.error());
      return;
    }
  } else if (txn_name == "stock_level") {
    if ((args.size() < 4 || args.size() > 5) || txn.code().procedures_size() != 2) {
      txn.set_status(TransactionStatus::ABORTED);
      txn.set_abort_reason("StockLevel Txn - Invalid number of arguments");
      return;
    }
    std::vector<int> w_ids = split_and_stoi(args[1]);
    std::vector<int> d_ids = split_and_stoi(args[2]);
    std::vector<int> o_ids = split_and_stoi(args[3]);

    size_t n = w_ids.size();
    if (d_ids.size() != n || o_ids.size() != n) {
      txn.set_status(TransactionStatus::ABORTED);
      txn.set_abort_reason("StockLevel Txn - Vector sizes do not match");
      return;
    }
    std::array<int, benchx::StockLevelTxn::kTotalItems> i_ids;
    const auto& item_ids = txn.code().procedures(1);
    if (txn.code().procedures(1).args_size() != benchx::StockLevelTxn::kTotalItems) {
      txn.set_status(TransactionStatus::ABORTED);
      txn.set_abort_reason("StockLevel Txn - Invalid number of items");
      return;
    }
    for (int i = 0; i < benchx::StockLevelTxn::kTotalItems; i++) {
      i_ids[i] = stoi(item_ids.args(i));
    }

    benchx::StockLevelTxn stock_level(txn_adapter, w_ids, d_ids, o_ids, i_ids);
    if (!stock_level.Execute()) {
      txn.set_status(TransactionStatus::ABORTED);
      txn.set_abort_reason("StockLevel Txn - " + stock_level.error());
      return;
    }
  } else {
    txn.set_status(TransactionStatus::ABORTED);
    txn.set_abort_reason("Unknown procedure name");
    return;
  }
  txn.set_status(TransactionStatus::COMMITTED);
  ApplyWrites(txn, sharder_, storage_);
}

}  // namespace slog
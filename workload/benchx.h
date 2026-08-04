#pragma once

#include <vector>

#include "common/configuration.h"
#include "common/types.h"
#include "execution/benchx/constants.h"
#include "execution/benchx/transaction.h"
#include "proto/transaction.pb.h"
#include "workload/workload.h"

using std::vector;

namespace slog {

class BenchXWorkload : public Workload {
 public:
  BenchXWorkload(const ConfigurationPtr& config, RegionId region, ReplicaId replica, const std::string& params_str,
                  std::pair<int, int> id_slot, const uint32_t seed = std::random_device()());

  std::pair<Transaction*, TransactionProfile> NextTransaction() override;

  bool HasDependentTxns() const override {
    for (auto p : dependent_percent_) {
      if (p > 0) return true;
    }
    return false;
  }

  // Called internally by the txn_generator to set the returned transaction 
  // from the first phase of a dependent transaction
  Transaction* prev_txn_ = nullptr;
  const TransactionProfile* prev_profile_ = nullptr;

  void SetCRDBCombinedMode(bool val) { crdb_combined_mode_ = val; }
  bool crdb_combined_mode_ = false;

 private:
  int local_region() { return config_->num_regions() == 1 ? local_replica_ : local_region_; }

  void NewOrder(Transaction& txn, TransactionProfile& pro, int w_id, int partition, int txn_type);
  void InsertOnly(Transaction& txn, TransactionProfile& pro, int w_id, int partition, int txn_type);
  void OrderStatus(Transaction& txn, TransactionProfile& pro, int w_id, int partition, int txn_type);
  void DeleteOnly(Transaction& txn, TransactionProfile& pro, int w_id, int partition, int txn_type);
  void StockLevel(Transaction& txn, TransactionProfile& pro, int w_id, int partition, int txn_type);

  std::vector<int> SelectRemoteWarehouses(int partition);
  int GetRegionFromWarehouse(int warehouse_id);

  // Buffer to hold retrieved customer IDs for dependent OrderStatus
  std::vector<int> c_ids_to_retrieve_;

  std::vector<int> w_ids_to_retrieve_;
  std::vector<int> d_ids_to_retrieve_;
  std::vector<int> o_ids_to_retrieve_;

  // Buffer to hold state for dependent InsertOnly
  std::vector<int> io_w_ids_;
  std::vector<int> io_d_ids_;

  struct DeleteOnlyState {
    std::vector<int> w_ids;
    std::vector<int> d_ids;
    std::vector<int> c_w_ids;
    std::vector<int> c_d_ids;
    std::vector<int> h_ids;
  };

  struct NewOrderState {
    int w_id;
    int d_id;
    int o_id;
    int i_w_id;
    int64_t datetime;
    std::vector<benchx::OrderLine> ol;
  };

  struct StockLevelState {
    std::vector<int> w_ids;
    std::vector<int> d_ids;
    std::vector<int> o_ids;
  };

  struct InsertOnlyState {
    std::vector<int> w_ids;
    std::vector<int> d_ids;
    std::vector<int> c_w_ids;
    std::vector<int> c_d_ids;
    std::vector<int64_t> amounts;
    std::vector<int64_t> datetimes;
    std::vector<int> h_ids;
  };

  struct OrderStatusState {
    std::vector<int> w_ids;
    std::vector<int> d_ids;
    std::vector<int> c_ids;
    std::vector<int> o_ids;
  };

  std::unordered_map<TxnId, int> pending_txn_types_;
  std::unordered_map<TxnId, DeleteOnlyState> pending_del_states_;
  std::unordered_map<TxnId, NewOrderState> pending_no_states_;
  std::unordered_map<TxnId, StockLevelState> pending_sl_states_;
  std::unordered_map<TxnId, InsertOnlyState> pending_io_states_;
  std::unordered_map<TxnId, OrderStatusState> pending_os_states_;
  std::unordered_map<TxnId, uint64_t> pending_dep_ids_;

  uint64_t dep_txn_id_counter_ = 0;
  uint64_t current_dep_txn_id_ = 0;

  int prev_txn_type_ = 0;
  int max_items_;

  ConfigurationPtr config_;
  RegionId local_region_;
  ReplicaId local_replica_;
  std::vector<int> distance_ranking_;
  int zipf_coef_;
  // _warehouse vector has dimensions: partition (currently 2), home/home (2?, i.e., number of 'regions' blocks in the .conf file), and then a list of warehouses that are based there
  vector<vector<vector<int>>> warehouse_index_;
  std::mt19937 rg_;
  TxnId client_txn_id_counter_;
  std::vector<int> txn_mix_;

  struct BenchXIds {
    BenchXIds(int i = 1) : o_id(benchx::kOrdPerDist + i), no_o_id(benchx::kOrdPerDist - 900 + i), h_id(i + 1) {}
    int o_id;
    int no_o_id;
    int h_id;
  };

  class BenchXIdGenerator {
   public:
    BenchXIdGenerator() = default;
    BenchXIdGenerator(int w, int init, int step) : max_o_id_(benchx::kOrdPerDist), step_(step) {
      ids_.reserve(w);
      for (int i = 0; i < w; i++) {
        auto& warehouse = ids_.emplace_back();
        for (size_t j = 0; j < warehouse.size(); j++) {
          warehouse[j] = BenchXIds(init);
        }
      }
    }

    int NextOId(int w_id, int d_id) {
      auto o_id = ids_[w_id - 1][d_id - 1].o_id;
      ids_[w_id - 1][d_id - 1].o_id += step_;
      max_o_id_ = std::max(max_o_id_, o_id);
      return o_id;
    }

    int NextNOOId(int w_id, int d_id) { 
      auto no_o_id = ids_[w_id - 1][d_id - 1].no_o_id;
      ids_[w_id - 1][d_id - 1].no_o_id += step_;
      return no_o_id;
    }

    int NextHId(int w_id, int d_id) { 
      auto h_id = ids_[w_id - 1][d_id - 1].h_id;
      ids_[w_id - 1][d_id - 1].h_id += step_;
      return h_id;
    }

    int max_o_id() const { return max_o_id_; }

    int max_h_id() const {
      int max_h = 0;
      for (const auto& warehouse : ids_) {
        for (const auto& dist : warehouse) {
          max_h = std::max(max_h, dist.h_id);
        }
      }
      return max_h;
    }

   private:
    std::vector<std::array<BenchXIds, benchx::kDistPerWare>> ids_;
    int max_o_id_;
    int step_;
  } id_generator_;

  private:
  std::vector<int> lsh_percent_;
  std::vector<int> fsh_percent_;
  std::vector<int> mh_percent_;
  std::vector<int> dependent_percent_;
  std::vector<double> intensity_;
  std::vector<double> skew_;
};

}  // namespace slog
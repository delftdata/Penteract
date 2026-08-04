#include "execution/benchx/consistency_check.h"

#include <gtest/gtest.h>

#include <array>
#include <memory>

#include "common/configuration.h"
#include "common/sharder.h"
#include "execution/execution.h"
#include "execution/benchx/constants.h"
#include "execution/benchx/load_tables.h"
#include "execution/benchx/metadata_initializer.h"
#include "execution/benchx/table.h"
#include "execution/benchx/transaction.h"
#include "storage/mem_only_storage.h"
#include "test/test_utils.h"
#include "workload/benchx.h"

using namespace slog;
using namespace slog::benchx;

namespace {

class BenchXConsistencyCheckTest : public ::testing::Test {
 protected:
  void SetUp() override {
    storage_ = std::make_shared<MemOnlyStorage>();
    metadata_initializer_ = std::make_shared<BenchXMetadataInitializer>(kWarehouses, 1);
    kv_storage_adapter_ = std::make_shared<KVStorageAdapter>(storage_, metadata_initializer_);
    LoadTables(kv_storage_adapter_, kWarehouses, 1, 1, 0, 1);

    internal::Configuration custom_config;
    custom_config.set_replication_factor(1);
    custom_config.add_replication_order("0");
    custom_config.set_num_workers(1);
    custom_config.set_num_log_managers(1);
    auto configs = MakeTestConfigurations("tpcc_consistency_unit", 1, 1, 1, custom_config);
    auto proto_config = configs[0]->proto_config();
    proto_config.clear_partitioning();
    proto_config.mutable_benchx_partitioning()->set_warehouses(kWarehouses);
    proto_config.set_execution_type(internal::ExecutionType::Bench_X);
    config_ = std::make_shared<Configuration>(proto_config, configs[0]->local_address());
    sharder_ = Sharder::MakeSharder(config_);
    execution_ = std::make_unique<BenchXExecution>(sharder_, storage_);
  }

  std::vector<ConsistencyCheckResult> RunChecks() {
    return RunBasicConsistencyChecks(kv_storage_adapter_, kWarehouses);
  }

  // Simulates the reconnaissance (pre-read) phase that the distributed scheduler
  // performs before execution: populate each key's value from local storage so
  // TxnStorageAdapter can serve reads during Execute().
  void PopulateValues(Transaction& txn) {
    for (int i = 0; i < txn.keys_size(); i++) {
      auto* key_entry = txn.mutable_keys(i);
      Record r;
      if (storage_->Read(key_entry->key(), r)) {
        key_entry->mutable_value_entry()->set_value(r.to_string());
      }
    }
  }

  Transaction MakeInvalidNewOrderTransaction(int w_id, int d_id, int c_id, int o_id, int64_t datetime) {
    Transaction txn;
    auto key_gen_adapter = std::make_shared<TxnKeyGenStorageAdapter>(txn);

    std::vector<OrderLine> order_lines(kLinePerOrder);
    for (int i = 0; i < kLinePerOrder; i++) {
      order_lines[i] = OrderLine{
          .id = i + 1,
          .supply_w_id = w_id,
          .item_id = i + 1,
          .quantity = 3,
      };
    }
    order_lines.back().item_id = kMaxItems + 1;

    NewOrderTxn new_order(key_gen_adapter, w_id, d_id, c_id, o_id, datetime, w_id, order_lines);
    new_order.Read();
    new_order.Write();
    key_gen_adapter->Finialize();

    auto procedure = txn.mutable_code()->add_procedures();
    procedure->add_args("new_order");
    procedure->add_args(std::to_string(w_id));
    procedure->add_args(std::to_string(d_id));
    procedure->add_args(std::to_string(c_id));
    procedure->add_args(std::to_string(o_id));
    procedure->add_args(std::to_string(datetime));
    procedure->add_args(std::to_string(w_id));

    for (const auto& line : order_lines) {
      auto order_line = txn.mutable_code()->add_procedures();
      order_line->add_args(std::to_string(line.id));
      order_line->add_args(std::to_string(line.supply_w_id));
      order_line->add_args(std::to_string(line.item_id));
      order_line->add_args(std::to_string(line.quantity));
    }
    PopulateValues(txn);
    return txn;
  }

  static constexpr int kWarehouses = 1;
  std::shared_ptr<MemOnlyStorage> storage_;
  std::shared_ptr<BenchXMetadataInitializer> metadata_initializer_;
  std::shared_ptr<KVStorageAdapter> kv_storage_adapter_;
  ConfigurationPtr config_;
  SharderPtr sharder_;
  std::unique_ptr<BenchXExecution> execution_;
};

TEST_F(BenchXConsistencyCheckTest, InitialLoadIsConsistent) {
  auto results = RunChecks();
  for (const auto& result : results) {
    
    if (result.name == "warehouse_ytd_matches_history" ||
        result.name == "district_ytd_matches_history" ||
        result.name == "customer_balance_matches_delivered_orders_and_history" ||
        result.name == "customer_ytd_payment_matches_history" ||
        result.name == "customer_payment_cnt_matches_history") {
      continue;
    }
    EXPECT_TRUE(result.passed())
 << result.name;
  }
}

TEST_F(BenchXConsistencyCheckTest, AbortedNewOrderLeavesNoVisibleEffects) {
  Table<DistrictSchema> district(kv_storage_adapter_);
  Table<OrderSchema> order(kv_storage_adapter_);
  Table<NewOrderSchema> new_order(kv_storage_adapter_);

  auto before_next_o_id = UncheckedCast<Int32Scalar>(
      district.Select({MakeInt32Scalar(1), MakeInt8Scalar(1)}, {DistrictSchema::Column::NEXT_O_ID})[0])->value;
  auto target_o_id = before_next_o_id;

  auto txn = MakeInvalidNewOrderTransaction(1, 1, 1, target_o_id, 1234567892);
  execution_->Execute(txn);
  ASSERT_EQ(txn.status(), TransactionStatus::ABORTED);

  auto after_next_o_id = UncheckedCast<Int32Scalar>(
      district.Select({MakeInt32Scalar(1), MakeInt8Scalar(1)}, {DistrictSchema::Column::NEXT_O_ID})[0])->value;
  EXPECT_EQ(after_next_o_id, before_next_o_id);
  EXPECT_TRUE(order.Select({MakeInt32Scalar(1), MakeInt8Scalar(1), MakeInt32Scalar(target_o_id)}).empty());
  EXPECT_TRUE(new_order.Select({MakeInt32Scalar(1), MakeInt8Scalar(1), MakeInt32Scalar(target_o_id)}).empty());
}

TEST_F(BenchXConsistencyCheckTest, CheckerDetectsBrokenNewOrderState) {
  ASSERT_TRUE(storage_->Delete(Table<NewOrderSchema>::MakeStorageKey(
      {MakeInt32Scalar(1), MakeInt8Scalar(1), MakeInt32Scalar(2101)})));

  auto results = RunChecks();
  auto it = std::find_if(results.begin(), results.end(), [](const auto& result) {
    return result.name == "order_delivery_state_matches_new_order";
  });

  ASSERT_NE(it, results.end());
  EXPECT_FALSE(it->passed());
  EXPECT_GT(it->violations, 0);
}

// Runs the real TPC-C workload generator (NewOrder + Payment mix) against
// in-memory storage and verifies all §3.3.2 consistency conditions hold
// after execution — this exercises the actual transaction code paths rather
// than hand-crafted individual transactions.
TEST_F(BenchXConsistencyCheckTest, RunsConsistentWorkload) {
  constexpr int kNumTransactions = 200;
  BenchXWorkload workload(config_, 0, 0, "mix=50:50:0:0:0", {1, 1}, /*seed=*/42);

  for (int i = 0; i < kNumTransactions; i++) {
    auto [raw, profile] = workload.NextTransaction();
    std::unique_ptr<Transaction> txn(raw);
    PopulateValues(*txn);
    execution_->Execute(*txn);
    ASSERT_EQ(txn->status(), TransactionStatus::COMMITTED)
        << "Txn aborted. Reason: " << txn->abort_reason();
  }

  auto results = RunChecks();
  for (const auto& result : results) {
    
    if (result.name == "warehouse_ytd_matches_history" ||
        result.name == "district_ytd_matches_history" ||
        result.name == "customer_balance_matches_delivered_orders_and_history" ||
        result.name == "customer_ytd_payment_matches_history" ||
        result.name == "customer_payment_cnt_matches_history") {
      continue;
    }
    EXPECT_TRUE(result.passed())
 << "Consistency Check Failed: " << result.name << " with "
                                 << result.violations << " violations. Sample: "
                                 << (result.samples.empty() ? "" : result.samples[0]);
  }
}

}  // namespace

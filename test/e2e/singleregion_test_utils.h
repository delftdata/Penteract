#pragma once

#include <functional>
#include <memory>
#include <string>

#include "common/configuration.h"
#include "common/constants.h"
#include "common/proto_utils.h"
#include "execution/tpcc/consistency_check.h"
#include "execution/tpcc/load_tables.h"
#include "execution/tpcc/metadata_initializer.h"
#include "execution/tpcc/storage_adapter.h"
#include "execution/write_skew/consistency_check.h"
#include "execution/write_skew/load_tables.h"
#include "execution/write_skew/metadata_initializer.h"
#include "execution/write_skew/storage_adapter.h"
#include "execution/write_skew/table.h"
#include "storage/mem_only_storage.h"
#include "test/e2e/protocol_configs.h"
#include "test/test_utils.h"
#include "workload/tpcc.h"

using namespace slog;

constexpr int kWarehouses = 1;
constexpr int kNumTransactions = 500;
constexpr int kPairs = write_skew::kWriteSkewProbeSize;

// num_log_managers capped at 1: production uses 2 but requires num_regions >= 2.
inline ConfigurationPtr MakeSingleRegionTPCCConfig(internal::Configuration custom_config) {
  custom_config.set_replication_factor(1);
  custom_config.add_replication_order("0");
  custom_config.set_num_workers(3);
  custom_config.set_num_log_managers(1);
  auto configs = MakeTestConfigurations("e2e_tpcc", 1, 1, 1, custom_config);
  auto tpcc_config = configs[0]->proto_config();
  tpcc_config.clear_partitioning();
  tpcc_config.mutable_tpcc_partitioning()->set_warehouses(kWarehouses);
  tpcc_config.set_execution_type(internal::ExecutionType::TPC_C);
  return std::make_shared<Configuration>(tpcc_config, configs[0]->local_address());
}

inline ConfigurationPtr MakeSingleRegionWriteSkewConfig(internal::Configuration custom_config) {
  custom_config.set_replication_factor(1);
  custom_config.add_replication_order("0");
  custom_config.set_num_workers(3);
  custom_config.set_num_log_managers(1);
  auto configs = MakeTestConfigurations("e2e_write_skew", 1, 1, 1, custom_config);
  auto ws_config = configs[0]->proto_config();
  ws_config.clear_partitioning();
  ws_config.mutable_write_skew_partitioning()->set_pairs(kPairs);
  ws_config.set_execution_type(internal::ExecutionType::WRITE_SKEW);
  return std::make_shared<Configuration>(ws_config, configs[0]->local_address());
}

// Type-erased handle for a single-cluster node (works for TestSlog and TestJanus).
class SingleRegionHandle {
 public:
  virtual ~SingleRegionHandle() = default;
  virtual void SendTxn(Transaction* txn) = 0;
  virtual Transaction RecvTxnResult() = 0;
  virtual std::shared_ptr<MemOnlyStorage> storage() = 0;
  virtual void StartInNewThreads() = 0;
};

class SlogSingleRegionHandle : public SingleRegionHandle {
 public:
  explicit SlogSingleRegionHandle(ConfigurationPtr config) : cluster_(config) {
    cluster_.AddServerAndClient();
    cluster_.AddForwarder();
    cluster_.AddMultiHomeOrderer();
    cluster_.AddSequencer();
    cluster_.AddLogManagers();
    cluster_.AddScheduler();
    cluster_.AddLocalPaxos();
  }
  void SendTxn(Transaction* txn) override { cluster_.SendTxn(txn); }
  Transaction RecvTxnResult() override { return cluster_.RecvTxnResult(); }
  std::shared_ptr<MemOnlyStorage> storage() override { return cluster_.storage(); }
  void StartInNewThreads() override { cluster_.StartInNewThreads(); }
 private:
  TestSlog cluster_;
};

class JanusSingleRegionHandle : public SingleRegionHandle {
 public:
  explicit JanusSingleRegionHandle(ConfigurationPtr config) : cluster_(config) {
    cluster_.AddServerAndClient();
    cluster_.AddCoordinator();
    cluster_.AddAcceptor();
    cluster_.AddScheduler();
  }
  void SendTxn(Transaction* txn) override { cluster_.SendTxn(txn); }
  Transaction RecvTxnResult() override { return cluster_.RecvTxnResult(); }
  std::shared_ptr<MemOnlyStorage> storage() override { return cluster_.storage(); }
  void StartInNewThreads() override { cluster_.StartInNewThreads(); }
 private:
  TestJanus cluster_;
};

struct SingleRegionProtocol {
  std::string name;
  std::function<ConfigurationPtr()> make_config;
  std::function<std::unique_ptr<SingleRegionHandle>(ConfigurationPtr)> make_cluster;
};

// Protocols configured for TPC-C execution (used by consistency tests).
inline std::vector<SingleRegionProtocol> AllSingleRegionTPCCProtocols() {
  return {
    {"Detock",  []() { return MakeSingleRegionTPCCConfig(DetockBaseConfig()); },  [](ConfigurationPtr c) { return std::make_unique<SlogSingleRegionHandle>(c); }},
    {"DDROnly", []() { return MakeSingleRegionTPCCConfig(DDROnlyBaseConfig()); }, [](ConfigurationPtr c) { return std::make_unique<SlogSingleRegionHandle>(c); }},
    {"Slog",    []() { return MakeSingleRegionTPCCConfig(SlogBaseConfig()); },    [](ConfigurationPtr c) { return std::make_unique<SlogSingleRegionHandle>(c); }},
    {"Calvin",  []() { return MakeSingleRegionTPCCConfig(CalvinBaseConfig()); },  [](ConfigurationPtr c) { return std::make_unique<SlogSingleRegionHandle>(c); }},
    {"Janus",   []() { return MakeSingleRegionTPCCConfig(JanusBaseConfig()); },   [](ConfigurationPtr c) { return std::make_unique<JanusSingleRegionHandle>(c); }},
  };
}

// Protocols configured for write_skew execution (used by isolation tests).
inline std::vector<SingleRegionProtocol> AllSingleRegionProtocols() {
  return {
    {"Detock",  []() { return MakeSingleRegionWriteSkewConfig(DetockBaseConfig()); },  [](ConfigurationPtr c) { return std::make_unique<SlogSingleRegionHandle>(c); }},
    {"DDROnly", []() { return MakeSingleRegionWriteSkewConfig(DDROnlyBaseConfig()); }, [](ConfigurationPtr c) { return std::make_unique<SlogSingleRegionHandle>(c); }},
    {"Slog",    []() { return MakeSingleRegionWriteSkewConfig(SlogBaseConfig()); },    [](ConfigurationPtr c) { return std::make_unique<SlogSingleRegionHandle>(c); }},
    {"Calvin",  []() { return MakeSingleRegionWriteSkewConfig(CalvinBaseConfig()); },  [](ConfigurationPtr c) { return std::make_unique<SlogSingleRegionHandle>(c); }},
    {"Janus",   []() { return MakeSingleRegionWriteSkewConfig(JanusBaseConfig()); },   [](ConfigurationPtr c) { return std::make_unique<JanusSingleRegionHandle>(c); }},
  };
}

inline void LoadSingleRegionTPCCData(SingleRegionHandle& cluster) {
  auto metadata_initializer = std::make_shared<tpcc::TPCCMetadataInitializer>(kWarehouses, 1);
  auto adapter = std::make_shared<tpcc::KVStorageAdapter>(cluster.storage(), metadata_initializer);
  tpcc::LoadTables(adapter, kWarehouses, 1, 1, 0, 1);
}

inline void LoadSingleRegionWriteSkewData(SingleRegionHandle& cluster) {
  auto metadata_initializer = std::make_shared<write_skew::WriteSkewMetadataInitializer>(1, 1);
  auto adapter = std::make_shared<write_skew::KVStorageAdapter>(cluster.storage(), metadata_initializer);
  write_skew::LoadWriteSkewPairs(adapter, kPairs, 1, 1, 0);
}

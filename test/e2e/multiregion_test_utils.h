#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "common/configuration.h"
#include "common/constants.h"
#include "common/proto_utils.h"
#include "common/types.h"
#include "test/e2e/protocol_configs.h"
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
#include "test/test_utils.h"
#include "workload/tpcc.h"

using namespace slog;

constexpr int kMRWarehouses = 4;
constexpr int kMRRegions = 2;
constexpr int kMRReplicas = 1;
constexpr int kMRPartitions = 2;
constexpr int kMRNumTransactions = 500;

inline std::vector<ConfigurationPtr> MakeAllMultiRegionTPCCConfigs(internal::Configuration custom_config) {
  custom_config.set_replication_factor(1);
  custom_config.add_replication_order("0");
  custom_config.set_num_workers(3);
  custom_config.set_num_log_managers(2);
  auto raw = MakeTestConfigurations("mr_tpcc", kMRRegions, kMRReplicas, kMRPartitions, custom_config);
  std::vector<ConfigurationPtr> result;
  result.reserve(raw.size());
  for (auto& cfg : raw) {
    auto proto = cfg->proto_config();
    proto.clear_partitioning();
    proto.mutable_tpcc_partitioning()->set_warehouses(kMRWarehouses);
    proto.set_execution_type(internal::ExecutionType::TPC_C);
    result.push_back(std::make_shared<Configuration>(proto, cfg->local_address()));
  }
  return result;
}

inline std::vector<ConfigurationPtr> MakeAllMultiRegionWriteSkewConfigs(internal::Configuration custom_config) {
  custom_config.set_replication_factor(1);
  custom_config.add_replication_order("0");
  custom_config.set_num_workers(3);
  custom_config.set_num_log_managers(2);
  auto raw = MakeTestConfigurations("mr_write_skew", kMRRegions, kMRReplicas, kMRPartitions, custom_config);
  std::vector<ConfigurationPtr> result;
  result.reserve(raw.size());
  for (auto& cfg : raw) {
    auto proto = cfg->proto_config();
    proto.clear_partitioning();
    proto.mutable_write_skew_partitioning()->set_pairs(write_skew::kWriteSkewProbeSize);
    proto.set_execution_type(internal::ExecutionType::WRITE_SKEW);
    result.push_back(std::make_shared<Configuration>(proto, cfg->local_address()));
  }
  return result;
}

// Merges reads and ScanPrefix across multiple partition adapters (read-only; used for post-run checks).
class MergedTPCCStorageAdapter : public tpcc::StorageAdapter {
 public:
  explicit MergedTPCCStorageAdapter(std::vector<tpcc::StorageAdapterPtr> adapters) : parts_(std::move(adapters)) {}
  const std::string* Read(const std::string& key) override {
    for (auto& a : parts_) { if (const auto* v = a->Read(key)) return v; }
    return nullptr;
  }
  std::vector<std::pair<std::string, std::string>> ScanPrefix(const std::string& prefix) override {
    std::vector<std::pair<std::string, std::string>> result;
    for (auto& a : parts_) {
      auto partial = a->ScanPrefix(prefix);
      result.insert(result.end(), partial.begin(), partial.end());
    }
    return result;
  }
  bool Insert(const std::string&, std::string&&) override { return false; }
  bool Update(const std::string&, std::function<void(std::string&)>&&) override { return false; }
  bool Delete(std::string&&) override { return false; }
 private:
  std::vector<tpcc::StorageAdapterPtr> parts_;
};

// Merges reads across multiple write_skew partition adapters (read-only; used for post-run checks).
class MergedWriteSkewStorageAdapter : public write_skew::StorageAdapter {
 public:
  explicit MergedWriteSkewStorageAdapter(std::vector<write_skew::StorageAdapterPtr> adapters)
      : parts_(std::move(adapters)) {}
  const std::string* Read(const std::string& key) override {
    for (auto& a : parts_) { if (const auto* v = a->Read(key)) return v; }
    return nullptr;
  }
  std::vector<std::pair<std::string, std::string>> ScanPrefix(const std::string& prefix) override {
    std::vector<std::pair<std::string, std::string>> result;
    for (auto& a : parts_) {
      auto partial = a->ScanPrefix(prefix);
      result.insert(result.end(), partial.begin(), partial.end());
    }
    return result;
  }
  bool Insert(const std::string&, std::string&&) override { return false; }
  bool Update(const std::string&, std::function<void(std::string&)>&&) override { return false; }
  bool Delete(std::string&&) override { return false; }
 private:
  std::vector<write_skew::StorageAdapterPtr> parts_;
};

// Keep the old MergedStorageAdapter type alias for backward compatibility with tpcc tests
using MergedStorageAdapter = MergedTPCCStorageAdapter;

// Type-erased handle for a multi-region cluster.
class MultiRegionClusterHandle {
 public:
  virtual ~MultiRegionClusterHandle() = default;
  virtual void SendTxn(Transaction* txn) = 0;
  virtual Transaction RecvTxnResult() = 0;
  virtual std::shared_ptr<MemOnlyStorage> storage(int reg, int part) = 0;
  virtual void StartInNewThreads() = 0;
};

class MultiRegionSlogCluster : public MultiRegionClusterHandle {
 public:
  explicit MultiRegionSlogCluster(const std::vector<ConfigurationPtr>& configs) : main_(MakeMachineId(0, 0, 0)) {
    int counter = 0;
    for (int reg = 0; reg < kMRRegions; reg++) {
      for (int rep = 0; rep < kMRReplicas; rep++) {
        for (int part = 0; part < kMRPartitions; part++) {
          auto id = MakeMachineId(reg, rep, part);
          auto cfg = configs[counter++];
          auto& slog = nodes_.emplace(id, cfg).first->second;
          slog.AddServerAndClient();
          slog.AddForwarder();
          slog.AddMultiHomeOrderer();
          slog.AddSequencer();
          slog.AddLogManagers();
          slog.AddScheduler();
          slog.AddLocalPaxos();
          if (cfg->leader_region_for_multi_home_ordering() == cfg->local_region()) slog.AddGlobalPaxos();
        }
      }
    }
  }
  void StartInNewThreads() override { for (auto& [_, s] : nodes_) s.StartInNewThreads(); }
  void SendTxn(Transaction* txn) override { nodes_.at(main_).SendTxn(txn); }
  Transaction RecvTxnResult() override { return nodes_.at(main_).RecvTxnResult(); }
  std::shared_ptr<MemOnlyStorage> storage(int reg, int part) override {
    return nodes_.at(MakeMachineId(reg, 0, part)).storage();
  }
 private:
  std::map<MachineId, TestSlog> nodes_;
  MachineId main_;
};

class MultiRegionJanusCluster : public MultiRegionClusterHandle {
 public:
  explicit MultiRegionJanusCluster(const std::vector<ConfigurationPtr>& configs) : main_(MakeMachineId(0, 0, 0)) {
    int counter = 0;
    for (int reg = 0; reg < kMRRegions; reg++) {
      for (int rep = 0; rep < kMRReplicas; rep++) {
        for (int part = 0; part < kMRPartitions; part++) {
          auto id = MakeMachineId(reg, rep, part);
          auto& janus = nodes_.emplace(id, configs[counter++]).first->second;
          janus.AddServerAndClient();
          janus.AddCoordinator();
          janus.AddAcceptor();
          janus.AddScheduler();
        }
      }
    }
  }
  void StartInNewThreads() override { for (auto& [_, j] : nodes_) j.StartInNewThreads(); }
  void SendTxn(Transaction* txn) override { nodes_.at(main_).SendTxn(txn); }
  Transaction RecvTxnResult() override { return nodes_.at(main_).RecvTxnResult(); }
  std::shared_ptr<MemOnlyStorage> storage(int reg, int part) override {
    return nodes_.at(MakeMachineId(reg, 0, part)).storage();
  }
 private:
  std::map<MachineId, TestJanus> nodes_;
  MachineId main_;
};

struct MultiRegionProtocol {
  std::string name;
  std::function<std::vector<ConfigurationPtr>()> make_configs;
  std::function<std::unique_ptr<MultiRegionClusterHandle>(const std::vector<ConfigurationPtr>&)> make_cluster;
};

// Protocols configured for TPC-C execution (used by consistency tests).
inline std::vector<MultiRegionProtocol> AllMultiRegionTPCCProtocols() {
  using Configs = const std::vector<ConfigurationPtr>&;
  return {
    {"Detock",  []() { return MakeAllMultiRegionTPCCConfigs(DetockBaseConfig()); },  [](Configs c) { return std::make_unique<MultiRegionSlogCluster>(c); }},
    {"DDROnly", []() { return MakeAllMultiRegionTPCCConfigs(DDROnlyBaseConfig()); }, [](Configs c) { return std::make_unique<MultiRegionSlogCluster>(c); }},
    {"Slog",    []() { return MakeAllMultiRegionTPCCConfigs(SlogBaseConfig()); },    [](Configs c) { return std::make_unique<MultiRegionSlogCluster>(c); }},
    {"Calvin",  []() { return MakeAllMultiRegionTPCCConfigs(CalvinBaseConfig()); },  [](Configs c) { return std::make_unique<MultiRegionSlogCluster>(c); }},
    {"Janus",   []() { return MakeAllMultiRegionTPCCConfigs(JanusBaseConfig()); },   [](Configs c) { return std::make_unique<MultiRegionJanusCluster>(c); }},
  };
}

// Protocols configured for write_skew execution (used by isolation tests).
inline std::vector<MultiRegionProtocol> AllMultiRegionProtocols() {
  using Configs = const std::vector<ConfigurationPtr>&;
  return {
    {"Detock",  []() { return MakeAllMultiRegionWriteSkewConfigs(DetockBaseConfig()); },  [](Configs c) { return std::make_unique<MultiRegionSlogCluster>(c); }},
    {"DDROnly", []() { return MakeAllMultiRegionWriteSkewConfigs(DDROnlyBaseConfig()); }, [](Configs c) { return std::make_unique<MultiRegionSlogCluster>(c); }},
    {"Slog",    []() { return MakeAllMultiRegionWriteSkewConfigs(SlogBaseConfig()); },    [](Configs c) { return std::make_unique<MultiRegionSlogCluster>(c); }},
    {"Calvin",  []() { return MakeAllMultiRegionWriteSkewConfigs(CalvinBaseConfig()); },  [](Configs c) { return std::make_unique<MultiRegionSlogCluster>(c); }},
    {"Janus",   []() { return MakeAllMultiRegionWriteSkewConfigs(JanusBaseConfig()); },   [](Configs c) { return std::make_unique<MultiRegionJanusCluster>(c); }},
  };
}

template <typename ClusterT>
void LoadMultiRegionTPCCData(ClusterT& cluster) {
  auto metadata_init = std::make_shared<tpcc::TPCCMetadataInitializer>(kMRRegions, kMRPartitions);
  for (int reg = 0; reg < kMRRegions; reg++) {
    for (int part = 0; part < kMRPartitions; part++) {
      auto adapter = std::make_shared<tpcc::KVStorageAdapter>(cluster.storage(reg, part), metadata_init);
      tpcc::LoadTables(adapter, kMRWarehouses, kMRRegions, kMRPartitions, part, 1);
    }
  }
}

template <typename ClusterT>
void LoadMultiRegionWriteSkewData(ClusterT& cluster) {
  auto metadata_init = std::make_shared<write_skew::WriteSkewMetadataInitializer>(kMRRegions, kMRPartitions);
  for (int reg = 0; reg < kMRRegions; reg++) {
    for (int part = 0; part < kMRPartitions; part++) {
      auto adapter = std::make_shared<write_skew::KVStorageAdapter>(cluster.storage(reg, part), metadata_init);
      write_skew::LoadWriteSkewPairs(adapter, write_skew::kWriteSkewProbeSize, kMRRegions, kMRPartitions, part);
    }
  }
}

template <typename ClusterT>
tpcc::StorageAdapterPtr MakeMergedAdapter(ClusterT& cluster) {
  auto metadata_init = std::make_shared<tpcc::TPCCMetadataInitializer>(kMRRegions, kMRPartitions);
  std::vector<tpcc::StorageAdapterPtr> parts;
  for (int part = 0; part < kMRPartitions; part++) {
    parts.push_back(std::make_shared<tpcc::KVStorageAdapter>(cluster.storage(0, part), metadata_init));
  }
  return std::make_shared<MergedTPCCStorageAdapter>(std::move(parts));
}

template <typename ClusterT>
write_skew::StorageAdapterPtr MakeMergedWriteSkewAdapter(ClusterT& cluster) {
  auto metadata_init = std::make_shared<write_skew::WriteSkewMetadataInitializer>(kMRRegions, kMRPartitions);
  std::vector<write_skew::StorageAdapterPtr> parts;
  for (int part = 0; part < kMRPartitions; part++) {
    parts.push_back(std::make_shared<write_skew::KVStorageAdapter>(cluster.storage(0, part), metadata_init));
  }
  return std::make_shared<MergedWriteSkewStorageAdapter>(std::move(parts));
}

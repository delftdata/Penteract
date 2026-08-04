#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "execution/tpcc/consistency_check.h"
#include "test/e2e/singleregion_test_utils.h"

using namespace std;

namespace {

void ExpectConsistentTPCCState(SingleRegionHandle& cluster, const ConfigurationPtr& config) {
  TPCCWorkload workload(config, 0, 0, "mix=50:50:0:0:0", {1, 1});
  for (int i = 0; i < kNumTransactions; i++) {
    auto [txn, profile] = workload.NextTransaction();
    cluster.SendTxn(txn);
  }
  for (int i = 0; i < kNumTransactions; i++) {
    auto resp = cluster.RecvTxnResult();
    ASSERT_EQ(resp.status(), TransactionStatus::COMMITTED) << "Txn aborted: " << resp.abort_reason();
  }
  auto metadata_initializer = std::make_shared<tpcc::TPCCMetadataInitializer>(kWarehouses, 1);
  auto adapter = std::make_shared<tpcc::KVStorageAdapter>(cluster.storage(), metadata_initializer);
  auto results = tpcc::RunBasicConsistencyChecks(adapter, kWarehouses);
  for (const auto& result : results) {
    EXPECT_TRUE(result.passed()) << result.name << ": " << result.violations << " violations. "
                                 << (result.samples.empty() ? "" : result.samples[0]);
  }
}

}  // namespace

class ConsistencySingleRegionTest : public ::testing::TestWithParam<SingleRegionProtocol> {
 protected:
  void SetUp() override {
    config_ = GetParam().make_config();
    cluster_ = GetParam().make_cluster(config_);
    LoadSingleRegionTPCCData(*cluster_);
    cluster_->StartInNewThreads();
  }
  ConfigurationPtr config_;
  std::unique_ptr<SingleRegionHandle> cluster_;
};

TEST_P(ConsistencySingleRegionTest, RunsConsistentWorkload) {
  ExpectConsistentTPCCState(*cluster_, config_);
}

INSTANTIATE_TEST_SUITE_P(
    AllProtocols, ConsistencySingleRegionTest,
    ::testing::ValuesIn(AllSingleRegionTPCCProtocols()),
    [](const ::testing::TestParamInfo<SingleRegionProtocol>& info) { return info.param.name; });

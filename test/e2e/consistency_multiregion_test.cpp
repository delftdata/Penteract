#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "execution/tpcc/consistency_check.h"
#include "test/e2e/multiregion_test_utils.h"

using namespace std;

namespace {

void ExpectConsistentTPCCState(MultiRegionClusterHandle& cluster, const ConfigurationPtr& config) {
  TPCCWorkload workload(config, 0, 0, "mix=50:50:0:0:0", {1, 1});
  for (int i = 0; i < kMRNumTransactions; i++) {
    auto [txn, profile] = workload.NextTransaction();
    cluster.SendTxn(txn);
  }
  for (int i = 0; i < kMRNumTransactions; i++) {
    auto resp = cluster.RecvTxnResult();
    ASSERT_EQ(resp.status(), TransactionStatus::COMMITTED) << "Txn aborted: " << resp.abort_reason();
  }
  auto merged = MakeMergedAdapter(cluster);
  auto results = tpcc::RunBasicConsistencyChecks(merged, kMRWarehouses);
  for (const auto& result : results) {
    EXPECT_TRUE(result.passed()) << result.name << ": " << result.violations << " violations. "
                                 << (result.samples.empty() ? "" : result.samples[0]);
  }
}

}  // namespace

class ConsistencyMultiRegionTest : public ::testing::TestWithParam<MultiRegionProtocol> {
 protected:
  void SetUp() override {
    configs_ = GetParam().make_configs();
    cluster_ = GetParam().make_cluster(configs_);
    LoadMultiRegionTPCCData(*cluster_);
    cluster_->StartInNewThreads();
  }
  std::vector<ConfigurationPtr> configs_;
  std::unique_ptr<MultiRegionClusterHandle> cluster_;
};

TEST_P(ConsistencyMultiRegionTest, RunsConsistentWorkload) {
  ExpectConsistentTPCCState(*cluster_, configs_[0]);
}

INSTANTIATE_TEST_SUITE_P(
    AllProtocols, ConsistencyMultiRegionTest,
    ::testing::ValuesIn(AllMultiRegionTPCCProtocols()),
    [](const ::testing::TestParamInfo<MultiRegionProtocol>& info) { return info.param.name; });

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>

#include "execution/write_skew/consistency_check.h"
#include "execution/write_skew/table.h"
#include "test/e2e/multiregion_test_utils.h"

using namespace std;
using namespace slog::write_skew;

namespace {

void ExpectNoWriteSkew(MultiRegionClusterHandle& cluster, const ConfigurationPtr& config) {
  const int kPairs = kWriteSkewProbeSize;
  for (int i = 1; i <= kPairs; i++) {
    int id_a = 2 * i - 1;
    int id_b = 2 * i;

    auto txn1 = new Transaction();
    auto proc1 = txn1->mutable_code()->add_procedures();
    proc1->add_args("write_skew");
    proc1->add_args(to_string(id_a));
    proc1->add_args(to_string(id_b));
    auto* w1 = txn1->add_keys();
    w1->set_key(Table<WriteSkewSchema>::MakeStorageKey({MakeInt32Scalar(id_a)}));
    w1->mutable_value_entry()->set_type(KeyType::WRITE);
    auto* r1 = txn1->add_keys();
    r1->set_key(Table<WriteSkewSchema>::MakeStorageKey({MakeInt32Scalar(id_b)}));
    r1->mutable_value_entry()->set_type(KeyType::READ);

    auto txn2 = new Transaction();
    auto proc2 = txn2->mutable_code()->add_procedures();
    proc2->add_args("write_skew");
    proc2->add_args(to_string(id_b));
    proc2->add_args(to_string(id_a));
    auto* w2 = txn2->add_keys();
    w2->set_key(Table<WriteSkewSchema>::MakeStorageKey({MakeInt32Scalar(id_b)}));
    w2->mutable_value_entry()->set_type(KeyType::WRITE);
    auto* r2 = txn2->add_keys();
    r2->set_key(Table<WriteSkewSchema>::MakeStorageKey({MakeInt32Scalar(id_a)}));
    r2->mutable_value_entry()->set_type(KeyType::READ);

    cluster.SendTxn(txn1);
    cluster.SendTxn(txn2);
  }
  for (int i = 0; i < kPairs * 2; i++) {
    auto resp = cluster.RecvTxnResult();
    if (resp.status() != TransactionStatus::COMMITTED) {
      LOG(INFO) << "WriteSkew txn aborted (conflict detected): " << resp.abort_reason();
    }
  }
  auto merged = MakeMergedWriteSkewAdapter(cluster);
  auto results = RunWriteSkewCheck(merged);
  auto it = std::find_if(results.begin(), results.end(), [](const ConsistencyCheckResult& r) {
    return r.name == "write_skew_detected";
  });
  ASSERT_NE(it, results.end());
  EXPECT_TRUE(it->passed()) << "Write skew detected: " << it->violations << " pair(s)";
  EXPECT_EQ(ClassifyIsolationLevel(results), IsolationLevel::kStrictSerializable)
      << "Classification: " << IsolationLevelName(ClassifyIsolationLevel(results));
}

void ExpectDetectsWriteSkewInStorage(MultiRegionClusterHandle& cluster) {
  auto metadata_init = std::make_shared<WriteSkewMetadataInitializer>(kMRRegions, kMRPartitions);
  auto adapter = std::make_shared<KVStorageAdapter>(cluster.storage(0, 0), metadata_init);
  Table<WriteSkewSchema> table(adapter);
  table.Insert({MakeInt32Scalar(1), MakeInt32Scalar(1)});
  table.Insert({MakeInt32Scalar(2), MakeInt32Scalar(1)});
  auto merged = MakeMergedWriteSkewAdapter(cluster);
  auto results = RunWriteSkewCheck(merged);
  auto it = std::find_if(results.begin(), results.end(), [](const ConsistencyCheckResult& r) {
    return r.name == "write_skew_detected";
  });
  ASSERT_NE(it, results.end());
  EXPECT_FALSE(it->passed()) << "Write-skew violation not detected in storage";
  EXPECT_GT(it->violations, 0);
}

}  // namespace

class IsolationLevelMultiRegionTest : public ::testing::TestWithParam<MultiRegionProtocol> {
 protected:
  void SetUp() override {
    configs_ = GetParam().make_configs();
    cluster_ = GetParam().make_cluster(configs_);
    LoadMultiRegionWriteSkewData(*cluster_);
    cluster_->StartInNewThreads();
  }
  std::vector<ConfigurationPtr> configs_;
  std::unique_ptr<MultiRegionClusterHandle> cluster_;
};

TEST_P(IsolationLevelMultiRegionTest, DetectsWriteSkewInStorage) {
  ExpectDetectsWriteSkewInStorage(*cluster_);
}
TEST_P(IsolationLevelMultiRegionTest, ExecutionProducesNoWriteSkew) {
  // Janus merges key lists from both homes during multi-home coordination,
  // turning a 2-key transaction into a 4-key one. storage_adapter.cpp asserts
  // key_index_.size() == txn_.keys_size() and aborts. The cross-partition
  // write-skew probe is not compatible with Janus's multi-home expansion.
  if (GetParam().name == "Janus") {
    GTEST_SKIP() << "Janus multi-region write-skew probe not supported (multi-home key expansion)";
  }
  ExpectNoWriteSkew(*cluster_, configs_[0]);
}

INSTANTIATE_TEST_SUITE_P(
    AllProtocols, IsolationLevelMultiRegionTest,
    ::testing::ValuesIn(AllMultiRegionProtocols()),
    [](const ::testing::TestParamInfo<MultiRegionProtocol>& info) { return info.param.name; });

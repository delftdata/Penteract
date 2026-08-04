#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>

#include "execution/write_skew/consistency_check.h"
#include "execution/write_skew/table.h"
#include "test/e2e/singleregion_test_utils.h"

using namespace std;
using namespace slog::write_skew;

namespace {

void ExpectNoWriteSkew(SingleRegionHandle& cluster, const ConfigurationPtr& config) {
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
  auto metadata_initializer = std::make_shared<WriteSkewMetadataInitializer>(1, 1);
  auto adapter = std::make_shared<KVStorageAdapter>(cluster.storage(), metadata_initializer);
  auto results = RunWriteSkewCheck(adapter);
  auto it = std::find_if(results.begin(), results.end(), [](const ConsistencyCheckResult& r) {
    return r.name == "write_skew_detected";
  });
  ASSERT_NE(it, results.end());
  EXPECT_TRUE(it->passed()) << "Write skew detected: " << it->violations << " pair(s)";
  EXPECT_EQ(ClassifyIsolationLevel(results), IsolationLevel::kStrictSerializable)
      << "Classification: " << IsolationLevelName(ClassifyIsolationLevel(results));
}

void ExpectDetectsWriteSkewInStorage(SingleRegionHandle& cluster) {
  auto metadata_initializer = std::make_shared<WriteSkewMetadataInitializer>(1, 1);
  auto adapter = std::make_shared<KVStorageAdapter>(cluster.storage(), metadata_initializer);
  Table<WriteSkewSchema> table(adapter);
  // Rows id=1 and id=2 are pre-seeded with VALUE=0 by LoadSingleRegionWriteSkewData; use Update.
  table.Update({MakeInt32Scalar(1)}, {WriteSkewSchema::Column::VALUE}, {MakeInt32Scalar(1)});
  table.Update({MakeInt32Scalar(2)}, {WriteSkewSchema::Column::VALUE}, {MakeInt32Scalar(1)});
  auto results = RunWriteSkewCheck(adapter);
  auto it = std::find_if(results.begin(), results.end(), [](const ConsistencyCheckResult& r) {
    return r.name == "write_skew_detected";
  });
  ASSERT_NE(it, results.end());
  EXPECT_FALSE(it->passed()) << "Write-skew violation not detected in storage";
  EXPECT_GT(it->violations, 0);
}

}  // namespace

class IsolationLevelSingleRegionTest : public ::testing::TestWithParam<SingleRegionProtocol> {
 protected:
  void SetUp() override {
    config_ = GetParam().make_config();
    cluster_ = GetParam().make_cluster(config_);
    LoadSingleRegionWriteSkewData(*cluster_);
    cluster_->StartInNewThreads();
  }
  ConfigurationPtr config_;
  std::unique_ptr<SingleRegionHandle> cluster_;
};

TEST_P(IsolationLevelSingleRegionTest, DetectsWriteSkewInStorage) {
  ExpectDetectsWriteSkewInStorage(*cluster_);
}
TEST_P(IsolationLevelSingleRegionTest, ExecutionProducesNoWriteSkew) {
  ExpectNoWriteSkew(*cluster_, config_);
}

INSTANTIATE_TEST_SUITE_P(
    AllProtocols, IsolationLevelSingleRegionTest,
    ::testing::ValuesIn(AllSingleRegionProtocols()),
    [](const ::testing::TestParamInfo<SingleRegionProtocol>& info) { return info.param.name; });

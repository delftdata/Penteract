#include "workload/smallbank.h"

#include <glog/logging.h>

#include <algorithm>
#include <random>
#include <set>

#include "common/proto_utils.h"
#include "execution/smallbank/transaction.h"

using std::bernoulli_distribution;
using std::iota;
using std::sample;
using std::to_string;
using std::unordered_set;

namespace slog {
namespace {

constexpr char MH[] = "mh";
constexpr char MP[] = "mp";
constexpr char TXN_MIX[] = "mix";
constexpr char HOT[] = "hot";
constexpr char SUNFLOWER_TARGET_PROBABILITY[] = "sunflower_target_probability";
constexpr char SUNFLOWER_TARGET_REGION[] = "sunflower_target_region";

const RawParamMap DEFAULT_PARAMS = {{MH, "50"},
                                    {MP, "50"},
                                    //{TXN_MIX, "40:25:15:5:15"}, // Ratio used in BSc thesis project
                                    {TXN_MIX, "20:20:20:20:20"},  // Ratio 'most commonly' used in org. paper
                                    {HOT, "0.0"},
                                    {SUNFLOWER_TARGET_PROBABILITY, ""},
                                    {SUNFLOWER_TARGET_REGION, ""}};

struct TxnCounters {
  int total = 0;
  int sh = 0, mh = 0, sp = 0, mp = 0;
  float avg_size = 0.0;
};

struct TxnStats {
  TxnCounters balance, deposit, saving, amalgamate, writecheck;
  int sent_sunflower = 0;
} stats;

int total_txn_count = 0;

template <typename G>
int NURand(G& g, int A, int x, int y) {
  std::uniform_int_distribution<> rand1(0, A);
  std::uniform_int_distribution<> rand2(x, y);
  return (rand1(g) | rand2(g)) % (y - x + 1) + x;
}

template <typename G, typename T>
T SkewedPick(G& gen, const std::vector<T>& vec, double skew) {
  int size = vec.size();
  int A = skew * size;
  int idx = NURand(gen, A, 0, size - 1);
  return vec[idx];
}

template <typename G>
bool rollWithProbability(G& gen, double x) {
  std::uniform_real_distribution<> dist(0.0, 1.0);
  double randValue = dist(gen);
  return randValue < (x / 100.0);
}

template <typename G>
int mp_probabilityCalculator(G& gen, double prob_mp) {
  bool mp = rollWithProbability(gen, prob_mp);
  int choice = 1;
  if (mp) {
    choice = 2;
  }
  return choice;
}

uint32_t murmurhash3(const std::string& str) {
  uint32_t seed = 42;
  uint32_t hash = seed;
  const char* data = str.c_str();
  int len = str.length();

  const uint32_t c1 = 0xcc9e2d51;
  const uint32_t c2 = 0x1b873593;

  int i = 0;
  while (len >= 4) {
    uint32_t k = *(reinterpret_cast<const uint32_t*>(data + i));
    k *= c1;
    k = (k << 15) | (k >> 17);  // ROTL32(k, 15)
    k *= c2;

    hash ^= k;
    hash = (hash << 13) | (hash >> 19);  // ROTL32(hash, 13)
    hash = hash * 5 + 0xe6546b64;
    len -= 4;
    i += 4;
  }

  uint32_t tail = 0;
  switch (len) {
    case 3:
      tail ^= data[i + 2] << 16;
      break;
    case 2:
      tail ^= data[i + 1] << 8;
      break;
    case 1:
      tail ^= data[i];
      break;
  }

  tail *= c1;
  tail = (tail << 15) | (tail >> 17);
  tail *= c2;
  hash ^= tail;

  hash ^= str.length();
  hash ^= (hash >> 16);
  hash *= 0x85ebca6b;
  hash ^= (hash >> 13);
  hash *= 0xc2b2ae35;
  hash ^= (hash >> 16);

  return hash;
}

void TrackChoices(int choice, int& sh, int& mh, int& sp, int& mp) {
  switch (choice) {
    case 1:
      sh++;
      sp++;
      break;
    case 2:
      sh++;
      mp++;
      break;
  }
}
void PrintTxnTypeStats(const std::string& label, const TxnCounters& counters) {
  LOG(INFO) << label << " -> " << " SH: " << counters.sh << " MH: " << counters.mh << " SP: " << counters.sp
            << " MP: " << counters.mp << " TOTAL: " << counters.total << " AVG Size: " << counters.avg_size;
}

void LogTxnStats(const TxnStats& stats) {
  PrintTxnTypeStats("BALANCE", stats.balance);
  PrintTxnTypeStats("DEPOSIT", stats.deposit);
  PrintTxnTypeStats("SAVING", stats.saving);
  PrintTxnTypeStats("AMALGAMATE", stats.amalgamate);
  PrintTxnTypeStats("WRITECHECK", stats.writecheck);
  LOG(INFO) << "SUNFLOWER" << " -> " << stats.sent_sunflower;
}

// For the Calvin experiment, there is a single region, so replace the regions by the replicas so that
// we generate the same workload as other experiments
int GetNumRegions(const ConfigurationPtr& config) {
  return config->num_regions() == 1 ? config->num_replicas(config->local_region()) : config->num_regions();
}

}  // namespace

SmallBankWorkload::SmallBankWorkload(const ConfigurationPtr& config, RegionId region, ReplicaId replica,
                                     const string& params_str, std::pair<int, int> id_slot, const uint32_t seed)
    : Workload(DEFAULT_PARAMS, params_str),
      config_(config),
      local_region_((config->num_regions() == 1 ? replica : region)),
      local_replica_(replica),
      distance_ranking_(config->distance_ranking_from(region)),
      rg_(seed),
      client_txn_id_counter_(0),
      pending_balance_txn_(nullptr),
      pending_saving_txn_(nullptr),
      pending_deposit_txn_(nullptr),
      pending_writecheck_txn_(nullptr),
      pending_amalgamate_txn_(nullptr),
      previous_amalgamate_txn_(nullptr) {
  dep_txn_id_counter_ = (static_cast<uint64_t>(id_slot.first) * 1000 + static_cast<uint64_t>(id_slot.second)) * 1000000000ULL;
  name_ = "smallbank";
  CHECK(config_->proto_config().has_smallbank_partitioning())
      << "small_bank workload is only compatible with small_bank partitioning";

  auto num_regions = GetNumRegions(config_);
  auto num_partitions = config_->num_partitions();
  auto num_clients = config_->proto_config().smallbank_partitioning().clients();

  LOG(INFO) << "SmallBank Workload Init: R" << static_cast<int>(local_region_) << " P"
            << static_cast<int>(local_replica_) << " | Total Clients: " << num_clients
            << " | Total Regions: " << config_->num_regions() << " | Total Partitions: " << num_partitions;

  for (int i = 0; i < num_regions; i++) {
    vector<vector<int>> ids(num_regions);
    client_partition_map_.push_back(ids);
  }

  for (int i = 0; i < num_regions; i++) {
    vector<std::string> ids;
    sh_sp_accounts_by_region_.push_back(ids);
    sh_mp_accounts_by_region_.push_back(ids);
    sunflower_sent_regions.push_back(0);
  }

  for (int i = 0; i < num_clients; i++) {
    std::string client_name = "Client" + std::to_string(i);
    client_name.resize(24, ' ');
    client_names_by_id_[i] = client_name;
    uint32_t name_hash = murmurhash3(std::to_string(i));

    uint32_t name_partition = (name_hash / num_regions) % num_partitions;
    uint32_t name_home = i % num_regions;

    uint32_t client_partition = (i / num_regions) % num_partitions;
    uint32_t id_home = i % num_regions;

    bool same_partition = (name_partition == client_partition);

    if (same_partition) {
      sh_sp_accounts_by_region_[name_home].push_back(client_name);
    } else {
      sh_mp_accounts_by_region_[name_home].push_back(client_name);
    }

    client_partition_map_[client_partition][id_home].push_back(i);
  }

  for (int p = 0; p < num_partitions; ++p) {
    for (int h = 0; h < num_regions; ++h) {
      LOG(INFO) << "Partition: " << p << " | Home: " << h << " | Client Count: " << client_partition_map_[p][h].size();
    }
  }

  for (int region = 0; region < num_regions; ++region) {
    LOG(INFO) << "Region " << region << " | SH-SP Accounts: " << sh_sp_accounts_by_region_[region].size()
              << " | SH-MP Accounts: " << sh_mp_accounts_by_region_[region].size();
  }

  auto txn_mix_str = Split(params_.GetString(TXN_MIX), ":");
  CHECK_EQ(txn_mix_str.size(), 5) << "There must be exactly 5 values for txn mix";
  for (const auto& t : txn_mix_str) {
    txn_mix_.push_back(std::stoi(t));
  }

  if (IsSunflowerEnabled()) {
    int sunflower_probability = std::stoi(params_.GetString(SUNFLOWER_TARGET_PROBABILITY));
    int target_region = std::stoi(params_.GetString(SUNFLOWER_TARGET_REGION));
    LOG(INFO) << "Sunflower Enabled: Target Region: " << target_region << ", Probability: " << sunflower_probability
              << "%";
  }
}

std::pair<Transaction*, TransactionProfile> SmallBankWorkload::NextTransaction() {
  TransactionProfile pro;

  pro.client_txn_id = client_txn_id_counter_;
  pro.is_multi_partition = false;
  pro.is_multi_home = false;
  pro.is_foreign_single_home = false;
  Transaction* txn = new Transaction();

  if (pending_balance_txn_ != nullptr) {
    CHECK(pending_balance_txn_->keys_size() == 1);
    memcpy(&returned_first_customer_id, pending_balance_txn_->keys(0).value_entry().value().data(), sizeof(int));
    Balance(*txn, pro, 2);
    stats.balance.total++;
    pro.transaction_type = TransactionProfile::TransactionType::NOTHING;
    pending_balance_txn_ = nullptr;

  } else if (pending_deposit_txn_ != nullptr) {
    CHECK(pending_deposit_txn_->keys_size() == 1);
    memcpy(&returned_first_customer_id, pending_deposit_txn_->keys(0).value_entry().value().data(), sizeof(int));
    DepositChecking(*txn, pro, 2);
    stats.deposit.total++;
    pro.transaction_type = TransactionProfile::TransactionType::NOTHING;
    pending_deposit_txn_ = nullptr;

  } else if (pending_saving_txn_ != nullptr) {
    memcpy(&returned_first_customer_id, pending_saving_txn_->keys(0).value_entry().value().data(), sizeof(int));
    CHECK(pending_saving_txn_->keys_size() == 1);
    TransactionSaving(*txn, pro, 2);
    stats.saving.total++;
    pro.transaction_type = TransactionProfile::TransactionType::NOTHING;
    pending_saving_txn_ = nullptr;

  } else if (pending_writecheck_txn_ != nullptr) {
    memcpy(&returned_first_customer_id, pending_writecheck_txn_->keys(0).value_entry().value().data(), sizeof(int));
    CHECK(pending_writecheck_txn_->keys_size() == 1);
    Writecheck(*txn, pro, 2);
    stats.writecheck.total++;
    pro.transaction_type = TransactionProfile::TransactionType::NOTHING;
    pending_writecheck_txn_ = nullptr;

  } else if (pending_amalgamate_txn_ != nullptr && previous_amalgamate_txn_ == nullptr) {
    pro.transaction_type = TransactionProfile::TransactionType::AMALGAMATE;
    Amalgamate(*txn, pro, 2);
    previous_amalgamate_txn_ = pending_amalgamate_txn_;  // special handling for amalgamate
    pending_amalgamate_txn_ = nullptr;

  } else if (pending_amalgamate_txn_ != nullptr && previous_amalgamate_txn_ != nullptr) {
    memcpy(&am_returned_first_customer_id, previous_amalgamate_txn_->keys(0).value_entry().value().data(), sizeof(int));
    memcpy(&am_returned_second_customer_id, pending_amalgamate_txn_->keys(0).value_entry().value().data(), sizeof(int));
    CHECK(pending_amalgamate_txn_->keys_size() == 1);
    CHECK(previous_amalgamate_txn_->keys_size() == 1);
    pending_amalgamate_txn_ = nullptr;
    previous_amalgamate_txn_ = nullptr;
    pro.transaction_type = TransactionProfile::TransactionType::NOTHING;
    Amalgamate(*txn, pro, 3);
    stats.amalgamate.total++;

  } else {
    dep_txn_id_counter_++;
    std::discrete_distribution<> select_smallbank_txn(txn_mix_.begin(), txn_mix_.end());
    int choice = select_smallbank_txn(rg_);
    switch (choice) {
      case 0:
        Balance(*txn, pro, 1);
        pro.transaction_type = TransactionProfile::TransactionType::BALANCE;
        break;
      case 1:
        DepositChecking(*txn, pro, 1);
        pro.transaction_type = TransactionProfile::TransactionType::DEPOSIT_CHECKING;
        break;
      case 2:
        TransactionSaving(*txn, pro, 1);
        pro.transaction_type = TransactionProfile::TransactionType::TRANSACTION_SAVING;
        break;
      case 3:
        Amalgamate(*txn, pro, 1);
        pro.transaction_type = TransactionProfile::TransactionType::AMALGAMATE;
        break;
      case 4:
        Writecheck(*txn, pro, 1);
        pro.transaction_type = TransactionProfile::TransactionType::WRITECHECK;
        break;
      default:
        LOG(FATAL) << "Invalid txn choice";
    }
  }
  total_txn_count++;
  if (total_txn_count % 100000 == 0) {
    LogTxnStats(stats);
  }

  txn->mutable_internal()->set_id(client_txn_id_counter_);
  client_txn_id_counter_++;

  if (txn->code().procedures_size() > 0) {
    txn->mutable_code()->mutable_procedures(0)->add_args("dep_" + std::to_string(dep_txn_id_counter_));
  }

  return {txn, pro};
}  // namespace slog

std::string SmallBankWorkload::PickAccountName(int choice, TransactionProfile& pro) {
  double skew = params_.GetDouble(HOT);
  bool prob = false;
  int target_region = local_region_;

  if (IsSunflowerEnabled()) {
    const auto& sunflower_probability = std::stoi(params_.GetString(SUNFLOWER_TARGET_PROBABILITY));
    prob = std::bernoulli_distribution(sunflower_probability / 100.0)(rg_);
    if (prob) {
      stats.sent_sunflower++;
      target_region = std::stoi(params_.GetString(SUNFLOWER_TARGET_REGION));
    }
  }

  std::string name;
  switch (choice) {
    case 1:
      name = SkewedPick(rg_, sh_sp_accounts_by_region_[target_region], skew);
      break;
    case 2:
      name = SkewedPick(rg_, sh_mp_accounts_by_region_[target_region], skew);
      break;
    default:
      LOG(FATAL) << "Invalid account selection choice: " << choice;
  }
  return name;
}

void SmallBankWorkload::GetCustomerIdByName(Transaction& txn, TransactionProfile& pro, int choice,
                                            const std::string& override_account_name) {
  std::string name = override_account_name.empty() ? PickAccountName(choice, pro) : override_account_name;

  auto txn_adapter = std::make_shared<smallbank::TxnKeyGenStorageAdapter>(txn);
  smallbank::GetCustomerIdByNameTxn getCustomerIdByNameTxn_txn(txn_adapter, name);
  getCustomerIdByNameTxn_txn.Read();
  txn_adapter->Finialize();

  auto procedure = txn.mutable_code()->add_procedures();
  procedure->add_args("getCustomerIdByName");
  procedure->add_args(name);
}

void SmallBankWorkload::Balance(Transaction& txn, TransactionProfile& pro, int phase) {
  CHECK(phase == 1 || phase == 2) << "Invalid phase for Balance transaction: " << phase;

  if (phase == 1) {
    int choice = mp_probabilityCalculator(rg_, params_.GetDouble(MP));
    GetCustomerIdByName(txn, pro, choice);
    TrackChoices(choice, stats.balance.sh, stats.balance.mh, stats.balance.sp, stats.balance.mp);
    pro.dependency_type = TransactionProfile::DependencyType::FIRST_PHASE;

  } else {
    auto txn_adapter = std::make_shared<smallbank::TxnKeyGenStorageAdapter>(txn);
    smallbank::BalanceTxn balance_txn(txn_adapter, client_names_by_id_[returned_first_customer_id],
                                      returned_first_customer_id);
    balance_txn.Read();
    balance_txn.Write();
    txn_adapter->Finialize();

    auto procedure = txn.mutable_code()->add_procedures();
    procedure->add_args("balance");
    procedure->add_args(client_names_by_id_[returned_first_customer_id]);
    procedure->add_args(to_string(returned_first_customer_id));

    // For wokload profiling purposes combine txn into a single string
    std::string txn_string;
    txn_string.append("getCustomerIdByName")
        .append(";")
        .append(client_names_by_id_[returned_first_customer_id])
        .append(";")  // First 2 lines come from the transaction in Phase 1
        .append("balance")
        .append(";")
        .append(client_names_by_id_[returned_first_customer_id])
        .append(";")
        .append(std::to_string(returned_first_customer_id))
        .append(";");

    // Analytically calculate the size of txn_string for the memory footprint
    size_t char_bytes = txn_string.capacity();     // allocated chars (includes unused)
    size_t string_overhead = sizeof(std::string);  // object overhead (on stack)
    size_t total_bytes = char_bytes + string_overhead;

    // SmallBank is a special case here, because we already increase the txn counts after phase 2 is sucessfully
    // completed
    stats.balance.avg_size =
        (stats.balance.total * stats.balance.avg_size + (float)total_bytes) / (stats.balance.total + 1);

    pro.dependency_type = TransactionProfile::DependencyType::SECOND_PHASE;
  }
}

void SmallBankWorkload::DepositChecking(Transaction& txn, TransactionProfile& pro, int phase) {
  CHECK(phase == 1 || phase == 2) << "Invalid phase for DepositChecking transaction: " << phase;
  if (phase == 1) {
    int choice = mp_probabilityCalculator(rg_, params_.GetDouble(MP));
    TrackChoices(choice, stats.deposit.sh, stats.deposit.mh, stats.deposit.sp, stats.deposit.mp);
    GetCustomerIdByName(txn, pro, choice);
    pro.dependency_type = TransactionProfile::DependencyType::FIRST_PHASE;

  } else {
    int amount = std::uniform_int_distribution<>(100, 10000)(rg_);
    auto txn_adapter = std::make_shared<smallbank::TxnKeyGenStorageAdapter>(txn);
    smallbank::DepositCheckingTxn DepositCheckingTxn_txn(txn_adapter, client_names_by_id_[returned_first_customer_id],
                                                         returned_first_customer_id, amount);
    DepositCheckingTxn_txn.Read();
    DepositCheckingTxn_txn.Write();
    txn_adapter->Finialize();

    auto procedure = txn.mutable_code()->add_procedures();
    procedure->add_args("depositChecking");
    procedure->add_args(client_names_by_id_[returned_first_customer_id]);
    procedure->add_args(to_string(returned_first_customer_id));
    procedure->add_args(to_string(amount));

    // For wokload profiling purposes combine txn into a single string
    std::string txn_string;
    txn_string.append("getCustomerIdByName")
        .append(";")
        .append(client_names_by_id_[returned_first_customer_id])
        .append(";")  // First 2 lines come from the transaction in Phase 1
        .append("depositChecking")
        .append(";")
        .append(client_names_by_id_[returned_first_customer_id])
        .append(";")
        .append(std::to_string(returned_first_customer_id))
        .append(";")
        .append(std::to_string(amount))
        .append(";");

    // Analytically calculate the size of txn_string for the memory footprint
    size_t char_bytes = txn_string.capacity();     // allocated chars (includes unused)
    size_t string_overhead = sizeof(std::string);  // object overhead (on stack)
    size_t total_bytes = char_bytes + string_overhead;

    // SmallBank is a special case here, because we already increase the txn counts after phase 2 is sucessfully
    // completed
    stats.deposit.avg_size =
        (stats.deposit.total * stats.deposit.avg_size + (float)total_bytes) / (stats.deposit.total + 1);

    pro.dependency_type = TransactionProfile::DependencyType::SECOND_PHASE;
  }
}

void SmallBankWorkload::TransactionSaving(Transaction& txn, TransactionProfile& pro, int phase) {
  CHECK(phase == 1 || phase == 2) << "Invalid phase for TransactionSaving transaction: " << phase;

  if (phase == 1) {
    int choice = mp_probabilityCalculator(rg_, params_.GetDouble(MP));
    TrackChoices(choice, stats.saving.sh, stats.saving.mh, stats.saving.sp, stats.saving.mp);
    GetCustomerIdByName(txn, pro, choice);
    pro.dependency_type = TransactionProfile::DependencyType::FIRST_PHASE;

  } else {
    int amount = std::uniform_int_distribution<>(100, 10000)(rg_);
    auto txn_adapter = std::make_shared<smallbank::TxnKeyGenStorageAdapter>(txn);
    smallbank::TransactionSavingTxn transactionSavingTxn(txn_adapter, client_names_by_id_[returned_first_customer_id],
                                                         returned_first_customer_id, amount);
    transactionSavingTxn.Read();
    transactionSavingTxn.Write();
    txn_adapter->Finialize();

    auto procedure = txn.mutable_code()->add_procedures();
    procedure->add_args("transactionSaving");
    procedure->add_args(client_names_by_id_[returned_first_customer_id]);
    procedure->add_args(std::to_string(returned_first_customer_id));
    procedure->add_args(std::to_string(amount));

    // For wokload profiling purposes combine txn into a single string
    std::string txn_string;
    txn_string.append("getCustomerIdByName")
        .append(";")
        .append(client_names_by_id_[returned_first_customer_id])
        .append(";")  // First 2 lines come from the transaction in Phase 1
        .append("transactionSaving")
        .append(";")
        .append(client_names_by_id_[returned_first_customer_id])
        .append(";")
        .append(to_string(returned_first_customer_id))
        .append(";")
        .append(to_string(amount))
        .append(";");

    // Analytically calculate the size of txn_string for the memory footprint
    size_t char_bytes = txn_string.capacity();     // allocated chars (includes unused)
    size_t string_overhead = sizeof(std::string);  // object overhead (on stack)
    size_t total_bytes = char_bytes + string_overhead;

    // SmallBank is a special case here, because we already increase the txn counts after phase 2 is sucessfully
    // completed
    stats.saving.avg_size =
        (stats.saving.total * stats.saving.avg_size + (float)total_bytes) / (stats.saving.total + 1);

    pro.dependency_type = TransactionProfile::DependencyType::SECOND_PHASE;
  }
}

void SmallBankWorkload::Amalgamate(Transaction& txn, TransactionProfile& pro, int phase) {
  CHECK(phase >= 1 && phase <= 3) << "Invalid phase for Amalgamate transaction: " << phase;

  const auto num_regions = GetNumRegions(config_);
  const double skew = params_.GetDouble(HOT);
  const double mp_prob = params_.GetDouble(MP);
  const double mh_prob = params_.GetDouble(MH);
  const int local_region = local_region_;
  const int isMultiPartition = mp_probabilityCalculator(rg_, mp_prob);
  std::uniform_int_distribution<> rand_home(0, num_regions - 1);

  if (phase == 1) {
    const auto& account_pool =
        (isMultiPartition == 2) ? sh_mp_accounts_by_region_[local_region] : sh_sp_accounts_by_region_[local_region];

    amalgamate_src_ = SkewedPick(rg_, account_pool, skew);
    GetCustomerIdByName(txn, pro, 0, amalgamate_src_);

    pro.dependency_type = TransactionProfile::DependencyType::FIRST_PHASE;
  } else if (phase == 2) {
    const auto num_regions = GetNumRegions(config_);
    const int src_home = am_returned_first_customer_id % num_regions;

    const bool is_multi_home = rollWithProbability(rg_, mh_prob);

    pro.is_multi_home = is_multi_home;

    int dst_name_region = src_home;
    if (is_multi_home) do {
        dst_name_region = rand_home(rg_);
      } while (dst_name_region == src_home);

    const int target_region_id = is_multi_home ? dst_name_region : local_region;

    const auto& account_pool = (isMultiPartition == 2) ? sh_mp_accounts_by_region_[target_region_id]
                                                       : sh_sp_accounts_by_region_[target_region_id];

    amalgamate_dst_ = SkewedPick(rg_, account_pool, skew);
    GetCustomerIdByName(txn, pro, 0, amalgamate_dst_);

    pro.dependency_type = TransactionProfile::DependencyType::FIRST_PHASE;
  } else {
    const int src_id = am_returned_first_customer_id;
    const int dst_id = am_returned_second_customer_id;
    const auto& src_name = client_names_by_id_[src_id];
    const auto& dst_name = client_names_by_id_[dst_id];
    const int src_home = src_id % num_regions;
    const int dst_home = dst_id % num_regions;

    if (src_home != dst_home) {
      stats.amalgamate.mh++;
      pro.is_multi_home = true;
    } else {
      stats.amalgamate.sh++;
      pro.is_multi_home = false;
    }

    auto txn_adapter = std::make_shared<smallbank::TxnKeyGenStorageAdapter>(txn);

    smallbank::AmalgamateTxn amalgamateTxn(txn_adapter, src_name, dst_name, src_id, dst_id);

    amalgamateTxn.Read();
    amalgamateTxn.Write();
    txn_adapter->Finialize();

    auto procedure = txn.mutable_code()->add_procedures();
    procedure->add_args("amalgamate");
    procedure->add_args(src_name);
    procedure->add_args(dst_name);
    procedure->add_args(std::to_string(src_id));
    procedure->add_args(std::to_string(dst_id));

    // For wokload profiling purposes combine txn into a single string
    std::string txn_string;
    txn_string.append("getCustomerIdByName")
        .append(";")
        .append(client_names_by_id_[src_id])
        .append(";")  // First 2 lines come from the transaction in Phase 1
        .append("getCustomerIdByName")
        .append(";")
        .append(client_names_by_id_[dst_id])
        .append(";")  // Next 2 lines come from the transaction in Phase 2
        .append("amalgamate")
        .append(";")
        .append(src_name)
        .append(";")
        .append(dst_name)
        .append(";")
        .append(std::to_string(src_id))
        .append(";")
        .append(std::to_string(dst_id))
        .append(";");

    // Analytically calculate the size of txn_string for the memory footprint
    size_t char_bytes = txn_string.capacity();     // allocated chars (includes unused)
    size_t string_overhead = sizeof(std::string);  // object overhead (on stack)
    size_t total_bytes = char_bytes + string_overhead;

    // SmallBank is a special case here, because we already increase the txn counts after phase 2 is sucessfully
    // completed
    stats.amalgamate.avg_size =
        (stats.amalgamate.total * stats.amalgamate.avg_size + (float)total_bytes) / (stats.amalgamate.total + 1);

    pro.dependency_type = TransactionProfile::DependencyType::SECOND_PHASE;
  }
}

void SmallBankWorkload::Writecheck(Transaction& txn, TransactionProfile& pro, int phase) {
  CHECK(phase == 1 || phase == 2) << "Invalid phase for Writecheck transaction: " << phase;

  if (phase == 1) {
    int choice = mp_probabilityCalculator(rg_, params_.GetDouble(MP));
    TrackChoices(choice, stats.writecheck.sh, stats.writecheck.mh, stats.writecheck.sp, stats.writecheck.mp);
    GetCustomerIdByName(txn, pro, choice);
    pro.dependency_type = TransactionProfile::DependencyType::FIRST_PHASE;
  } else {
    int amount = std::uniform_int_distribution<>(100, 10000)(rg_);
    auto txn_adapter = std::make_shared<smallbank::TxnKeyGenStorageAdapter>(txn);
    smallbank::WritecheckTxn writecheck_txn(txn_adapter, client_names_by_id_[returned_first_customer_id],
                                            returned_first_customer_id, amount);
    writecheck_txn.Read();
    writecheck_txn.Write();
    txn_adapter->Finialize();

    auto procedure = txn.mutable_code()->add_procedures();
    procedure->add_args("writecheck");
    procedure->add_args(client_names_by_id_[returned_first_customer_id]);
    procedure->add_args(to_string(returned_first_customer_id));
    procedure->add_args(to_string(amount));

    // For wokload profiling purposes combine txn into a single string
    std::string txn_string;
    txn_string.append("writecheck")
        .append(";")
        .append(client_names_by_id_[returned_first_customer_id])
        .append(";")
        .append(to_string(returned_first_customer_id))
        .append(";")
        .append(to_string(amount))
        .append(";");

    // Analytically calculate the size of txn_string for the memory footprint
    size_t char_bytes = txn_string.capacity();     // allocated chars (includes unused)
    size_t string_overhead = sizeof(std::string);  // object overhead (on stack)
    size_t total_bytes = char_bytes + string_overhead;

    // SmallBank is a special case here, because we already increase the txn counts after phase 2 is sucessfully
    // completed
    stats.writecheck.avg_size =
        (stats.writecheck.total * stats.writecheck.avg_size + (float)total_bytes) / (stats.writecheck.total + 1);

    pro.dependency_type = TransactionProfile::DependencyType::SECOND_PHASE;
  }
}

bool SmallBankWorkload::IsSunflowerEnabled() const {
  return !params_.GetString(SUNFLOWER_TARGET_PROBABILITY).empty() &&
         !params_.GetString(SUNFLOWER_TARGET_REGION).empty();
}
}  // namespace slog
#include "workload/benchx.h"

#include <glog/logging.h>

#include <cmath>
#include <random>
#include <set>

#include "common/proto_utils.h"
#include "execution/benchx/constants.h"
#include "execution/benchx/transaction.h"

using std::bernoulli_distribution;
using std::iota;
using std::sample;
using std::to_string;
using std::unordered_set;

namespace slog {
namespace {

// Partition that is used in a single-partition transaction.
// Use a negative number to select a random partition for each transaction
constexpr char PARTITION[] = "sp_partition";
// Max number of regions to select warehouse from
constexpr char HOMES[] = "homes";
// Zipf coefficient for selecting regions to access in a txn. Must be non-negative.
// The lower this is, the more uniform the regions are selected
constexpr char MH_ZIPF[] = "mh_zipf";
// Colon-separated list of % of the 5 txn types. Default is: "45:43:4:4:4"
constexpr char TXN_MIX[] = "mix";
// Only send single-home transactions
constexpr char SH_ONLY[] = "sh_only";
// Skewness of the workload. A theta value between 0.0 and 1.0. Use -1 for default skewing, 0 means uniform distribution, and 1 means all transactions go to the same warehouse.
constexpr char SKEW[] = "skew";
// Colon-separated list of % of the 5 txn types. Default is: "33:33:33:33:33"
constexpr char LSH_PERCENT[] = "lsh";
// Colon-separated list of % of the 5 txn types. Default is: "33:33:33:33:33"
constexpr char FSH_PERCENT[] = "fsh";
// Colon-separated list of % of the 5 txn types. Default is: "33:33:33:33:33"
constexpr char MH_PERCENT[] = "mh";
// Colon-separated list of % of dependent transactions for the 5 txn types. Default is: "0:0:0:0:0"
constexpr char DEPENDENT_PERCENT[] = "dependent_percent";
// Replace the individual NUM_* parameters with a single INTENSITY parameter
constexpr char INTENSITY[] = "intensity";
// Overrides the maximum number of items (default 100k)
constexpr char MAX_ITEMS[] = "max_items";

// To mimic TPC-C, where NewOrder will subsume Payment and Delivery, we set the default mix to "92:0:4:0:4" (i.e., 92%
// NewOrder, 4% OrderStatus, 4% StockLevel, and 0% InsertOnly and DeleteOnly). This is also the default mix used in the
// original BenchX implementation. For a more even mix, use "20:20:20:20:20".
const RawParamMap DEFAULT_PARAMS = {
    {PARTITION, "-1"},
    {HOMES, "2"},
    {MH_ZIPF, "0"},
    {TXN_MIX, "20:20:20:20:20"},
    {SH_ONLY, "0"},
    {SKEW, "0.0:0.0:0.0:0.0:0.0"},
    {LSH_PERCENT, "33:33:33:33:33"},
    {FSH_PERCENT, "33:33:33:33:33"},
    {MH_PERCENT, "34:34:34:34:34"},
    {DEPENDENT_PERCENT, "0:0:0:0:0"},
    {INTENSITY, "5:5:5:5:5"},
    {MAX_ITEMS, std::to_string(benchx::kMaxItems)},
};
// const RawParamMap DEFAULT_PARAMS = {{PARTITION, "-1"}, {HOMES, "2"}, {MH_ZIPF, "0"}, {TXN_MIX, "45:43:4:4:4"},
// {SH_ONLY, "0"}}; // Not sure why they had 45% and 43%?

int total_txn_count = 0;

int new_order_count = 0;
int lsh_no = 0;
int fsh_no = 0;
int mh_no = 0;
float avg_no_txn_size = 0.0;

int insert_only_count = 0;
int lsh_io = 0;
int fsh_io = 0;
int mh_io = 0;
float avg_io_txn_size = 0.0;

int delete_only_count = 0;
int lsh_del = 0;
int fsh_del = 0;
int mh_del = 0;
float avg_del_txn_size = 0.0;

int order_status_count = 0;
int lsh_os = 0;
int fsh_os = 0;
int mh_os = 0;
float avg_os_txn_size = 0.0;

int stock_level_count = 0;
int lsh_sl = 0;
int fsh_sl = 0;
int mh_sl = 0;
float avg_sl_txn_size = 0.0;

// TODO: Add default params from TPC-C skewness spec
int default_item_skewness = 8191;  // maxItems at 100k
int default_cust_skewness = 1023;  // maxCust per district at 3k

double org_item_skew = (double)default_item_skewness / benchx::kMaxItems;     // 0.08191
double org_cust_skew = (double)default_cust_skewness / benchx::kCustPerDist;  // 0.341

// Random number generator to
template <typename G>
int NURand(G& g, int A, int x, int y) {
  std::uniform_int_distribution<> rand1(0, A);
  std::uniform_int_distribution<> rand2(x, y);
  return (rand1(g) | rand2(g)) % (y - x + 1) + x;
}

template <typename T, typename G>
T SampleOnce(G& g, const std::vector<T>& source) {
  CHECK(!source.empty());
  size_t i = std::uniform_int_distribution<size_t>(0, source.size() - 1)(g);
  return source[i];
}

// Helper function to join a vector of ints into a comma-separated string
std::string Join(const std::vector<int>& vec, const std::string& delim) {
  if (vec.empty()) return "";
  std::string result = std::to_string(vec[0]);
  for (size_t i = 1; i < vec.size(); ++i) {
    result += delim + std::to_string(vec[i]);
  }
  return result;
}

// Helper function to join a vector of int64_t into a comma-separated string
std::string Join(const std::vector<int64_t>& vec, const std::string& delim) {
  if (vec.empty()) return "";
  std::string result = std::to_string(vec[0]);
  for (size_t i = 1; i < vec.size(); ++i) {
    result += delim + std::to_string(vec[i]);
  }
  return result;
}

// Helper function to join a vector of string into a comma-separated string
std::string Join(const std::vector<std::string>& vec, const std::string& delim) {
  if (vec.empty()) return "";
  std::string result = vec[0];
  for (size_t i = 1; i < vec.size(); ++i) {
    result += delim + vec[i];
  }
  return result;
}

// For the Calvin experiment, there is a single region, so replace the regions by the replicas so that
// we generate the same workload as other experiments
int GetNumRegions(const ConfigurationPtr& config) {
  return config->num_regions() == 1 ? config->num_replicas(config->local_region()) : config->num_regions();
}

}  // namespace

BenchXWorkload::BenchXWorkload(const ConfigurationPtr& config, RegionId region, ReplicaId replica,
                               const string& params_str, std::pair<int, int> id_slot, const uint32_t seed)
    : Workload(DEFAULT_PARAMS, params_str),
      config_(config),
      local_region_(region),
      local_replica_(replica),
      distance_ranking_(config->distance_ranking_from(region)),
      zipf_coef_(params_.GetInt32(MH_ZIPF)),
      rg_(seed),
      client_txn_id_counter_(0),
      prev_txn_(nullptr) {
  name_ = "benchx";
  CHECK(config_->proto_config().has_benchx_partitioning())
      << "BenchX workload is only compatible with BenchX partitioning";

  auto num_regions = GetNumRegions(config_);
  if (distance_ranking_.empty()) {
    for (int i = 0; i < num_regions; i++) {
      if (i != local_region()) {
        distance_ranking_.push_back(i);
      }
    }
    if (zipf_coef_ > 0) {
      LOG(WARNING) << "Distance ranking is not provided. MH_ZIPF is reset to 0.";
      zipf_coef_ = 0;
    }
  } else if (config_->num_regions() == 1) {
    // This case is for the Calvin experiment where there is only a single region.
    // The num_regions variable is equal to num_replicas at this point
    CHECK_EQ(distance_ranking_.size(), num_regions * (num_regions - 1));
    size_t from = local_region() * (num_regions - 1);
    std::copy_n(distance_ranking_.begin() + from, num_regions, distance_ranking_.begin());
    distance_ranking_.resize(num_regions - 1);
  }

  CHECK_EQ(distance_ranking_.size(), num_regions - 1) << "Distance ranking size must match the number of regions";

  auto num_partitions = config_->num_partitions();
  for (int i = 0; i < num_partitions; i++) {
    vector<vector<int>> partitions(num_regions);
    warehouse_index_.push_back(partitions);
  }
  auto num_warehouses = config_->proto_config().benchx_partitioning().warehouses();
  for (int i = 0; i < num_warehouses; i++) {
    int partition = i % num_partitions;
    int home = i / num_partitions % num_regions;
    warehouse_index_[partition][home].push_back(i + 1);
  }
  id_generator_ = BenchXIdGenerator(num_warehouses, id_slot.first, id_slot.second);
  dep_txn_id_counter_ = (static_cast<uint64_t>(id_slot.first) * 1000 + static_cast<uint64_t>(id_slot.second)) * 1000000000ULL;

  max_items_ = std::stoi(params_.GetString(MAX_ITEMS));

  auto txn_mix_str = Split(params_.GetString(TXN_MIX), ":");
  CHECK_EQ(txn_mix_str.size(), 5) << "There must be exactly 5 values for txn mix";
  for (const auto& t : txn_mix_str) {
    txn_mix_.push_back(std::stoi(t));
  }

  // Parse LSH_PERCENT, FSH_PERCENT, MH_PERCENT, NUM_READS, NUM_UPDATES, NUM_INSERTS, NUM_DELETES
  auto lsh_percent_str = Split(params_.GetString(LSH_PERCENT), ":");
  auto fsh_percent_str = Split(params_.GetString(FSH_PERCENT), ":");
  auto mh_percent_str = Split(params_.GetString(MH_PERCENT), ":");
  auto dependent_percent_str = Split(params_.GetString(DEPENDENT_PERCENT), ":");
  auto intensity_str = Split(params_.GetString(INTENSITY), ":");
  auto skew_str = Split(params_.GetString(SKEW), ":");

  CHECK_EQ(lsh_percent_str.size(), 5) << "There must be exactly 5 values for lsh_percent";
  CHECK_EQ(fsh_percent_str.size(), 5) << "There must be exactly 5 values for fsh_percent";
  CHECK_EQ(mh_percent_str.size(), 5) << "There must be exactly 5 values for mh_percent";
  CHECK_EQ(dependent_percent_str.size(), 5) << "There must be exactly 5 values for dependent_percent";
  CHECK_EQ(intensity_str.size(), 5) << "There must be exactly 5 values for intensity";

  for (int i = 0; i < 5; ++i) {
    lsh_percent_.push_back(std::stoi(lsh_percent_str[i]));
    fsh_percent_.push_back(std::stoi(fsh_percent_str[i]));
    mh_percent_.push_back(std::stoi(mh_percent_str[i]));
    dependent_percent_.push_back(std::stoi(dependent_percent_str[i]));
    intensity_.push_back(std::stod(intensity_str[i]));
    if (skew_str.size() == 1) {
      skew_.push_back(std::stod(skew_str[0]));
    } else {
      skew_.push_back(std::stod(skew_str[i]));
    }

    // Validate that LSH + FSH + MH = 100 for each transaction type
    CHECK_EQ(lsh_percent_[i] + fsh_percent_[i] + mh_percent_[i], 100)
        << "LSH_PERCENT[" << i << "] + FSH_PERCENT[" << i << "] + MH_PERCENT[" << i << "] must sum to 100";
  }
}

std::pair<Transaction*, TransactionProfile> BenchXWorkload::NextTransaction() {
  TransactionProfile pro;

  pro.client_txn_id = client_txn_id_counter_;
  pro.is_multi_partition = false;
  pro.is_multi_home = false;
  pro.is_foreign_single_home = false;

  auto num_partitions = config_->num_partitions();
  auto partition = params_.GetInt32(PARTITION);
  if (partition < 0) {
    partition = std::uniform_int_distribution<>(0, num_partitions - 1)(rg_);
  }

  const auto& selectable_w = warehouse_index_[partition][local_region()];
  CHECK(!selectable_w.empty()) << "Not enough warehouses";
  int w = SampleOnce(rg_, selectable_w);

  Transaction* txn = new Transaction();
  int txn_type;
  if (prev_txn_ != nullptr && prev_profile_ != nullptr) {
    txn_type = pending_txn_types_[prev_profile_->client_txn_id];
    pending_txn_types_.erase(prev_profile_->client_txn_id);
    if (txn_type == 0) {  // NewOrder
      NewOrder(*txn, pro, w, partition, txn_type);
    } else if (txn_type == 1) {  // InsertOnly
      InsertOnly(*txn, pro, w, partition, txn_type);
    } else if (txn_type == 2) {  // OrderStatus
      OrderStatus(*txn, pro, w, partition, txn_type);
    } else if (txn_type == 3) {  // DeleteOnly
      DeleteOnly(*txn, pro, w, partition, txn_type);
    } else if (txn_type == 4) {  // StockLevel
      StockLevel(*txn, pro, w, partition, txn_type);
    } else {
      LOG(FATAL) << "Invalid dependent txn choice";
    }
  } else {
    std::discrete_distribution<> select_benchx_txn(txn_mix_.begin(), txn_mix_.end());
    // LOG(INFO) << "Generating next transaction; partition: " << partition << ", warehouse: " << w;
    txn_type = select_benchx_txn(rg_);
    if (dependent_percent_[txn_type] > 0) {
      pending_txn_types_[pro.client_txn_id] = txn_type;
    }
    switch (txn_type) {
      case 0:
        NewOrder(*txn, pro, w, partition, txn_type);
        break;
      case 1:
        InsertOnly(*txn, pro, w, partition, txn_type);
        break;
      case 2:
        OrderStatus(*txn, pro, w, partition, txn_type);
        break;
      case 3:
        DeleteOnly(*txn, pro, w, partition, txn_type);
        break;
      case 4:
        StockLevel(*txn, pro, w, partition, txn_type);
        break;
      default:
        LOG(FATAL) << "Invalid txn choice";
        break;
    }
  }

  if (pro.dependency_type == TransactionProfile::DependencyType::FIRST_PHASE) {
    uint64_t new_dep_id = ++dep_txn_id_counter_;
    pending_dep_ids_[pro.client_txn_id] = new_dep_id;
    txn->mutable_code()->mutable_procedures(0)->add_args("dep_" + std::to_string(new_dep_id));
  } else if (pro.dependency_type == TransactionProfile::DependencyType::SECOND_PHASE) {
    uint64_t dep_id = pending_dep_ids_[prev_profile_->client_txn_id];
    txn->mutable_code()->mutable_procedures(0)->add_args("dep_" + std::to_string(dep_id));
    pending_dep_ids_.erase(prev_profile_->client_txn_id);
  }

  total_txn_count++;
  if (total_txn_count % 100000 == 0) {
    LOG(INFO) << "Current LSH txn counts: Total: " << (lsh_no + lsh_io + lsh_os + lsh_del + lsh_sl) << " NO: " << lsh_no
              << " Ins: " << lsh_io << " OS: " << lsh_os << " Del: " << lsh_del << " SL: " << lsh_sl;
    LOG(INFO) << "Current FSH txn counts: Total: " << (fsh_no + fsh_io + fsh_os + fsh_del + fsh_sl) << " NO: " << fsh_no
              << " Ins: " << fsh_io << " OS: " << fsh_os << " Del: " << fsh_del << " SL: " << fsh_sl;
    LOG(INFO) << "Current MH txn counts: Total: " << (mh_no + mh_io + mh_os + mh_del + mh_sl) << " NO: " << mh_no
              << " Ins: " << mh_io << " OS: " << mh_os << " Del: " << mh_del << " SL: " << mh_sl;
    LOG(INFO) << "Current LSH txn percentages: NO: " << round(100 * lsh_no / (double)new_order_count) << "%"
              << " Ins: " << round(100 * lsh_io / (double)insert_only_count) << "%"
              << " OS: " << round(100 * lsh_os / (double)order_status_count) << "%"
              << " Del: " << round(100 * lsh_del / (double)delete_only_count) << "%"
              << " SL: " << round(100 * lsh_sl / (double)stock_level_count) << "%";
    LOG(INFO) << "Current FSH txn percentages: NO: " << round(100 * fsh_no / (double)new_order_count) << "%"
              << " Ins: " << round(100 * fsh_io / (double)insert_only_count) << "%"
              << " OS: " << round(100 * fsh_os / (double)order_status_count) << "%"
              << " Del: " << round(100 * fsh_del / (double)delete_only_count) << "%"
              << " SL: " << round(100 * fsh_sl / (double)stock_level_count) << "%";
    LOG(INFO) << "Current MH txn percentages: NO: " << round(100 * mh_no / (double)new_order_count) << "%"
              << " Ins: " << round(100 * mh_io / (double)insert_only_count) << "%"
              << " OS: " << round(100 * mh_os / (double)order_status_count) << "%"
              << " Del: " << round(100 * mh_del / (double)delete_only_count) << "%"
              << " SL: " << round(100 * mh_sl / (double)stock_level_count) << "%";

    LOG(INFO) << "Current New Order txn counts: Total: " << new_order_count << ", " << avg_no_txn_size
              << " bytes on average";
    LOG(INFO) << "Current Insert Only txn counts: Total: " << insert_only_count << ", " << avg_io_txn_size
              << " bytes on average";
    LOG(INFO) << "Current Order Status txn counts: Total: " << order_status_count << ", " << avg_os_txn_size
              << " bytes on average";
    LOG(INFO) << "Current Delete Only txn counts: Total: " << delete_only_count << ", " << avg_del_txn_size
              << " bytes on average";
    LOG(INFO) << "Current Stock Level txn counts: Total: " << stock_level_count << ", " << avg_sl_txn_size
              << " bytes on average";
  }

  txn->mutable_internal()->set_id(client_txn_id_counter_);
  client_txn_id_counter_++;

  return {txn, pro};
}

void BenchXWorkload::NewOrder(Transaction& txn, TransactionProfile& pro, int w_id, int partition, int txn_type) {
  auto txn_adapter = std::make_shared<benchx::TxnKeyGenStorageAdapter>(txn);

  if (prev_txn_ != nullptr && prev_profile_ != nullptr) {
    int c_id;
    std::memcpy(&c_id, prev_txn_->keys(0).value_entry().value().data() + 39, sizeof(int));
    prev_txn_ = nullptr;
    pro.dependency_type = TransactionProfile::DependencyType::SECOND_PHASE;

    TxnId parent_txn_id = prev_profile_->client_txn_id;
    auto& state = pending_no_states_[parent_txn_id];
    int w_id_val = state.w_id;
    int d_id = state.d_id;
    int o_id = state.o_id;
    auto datetime = state.datetime;
    int i_w_id = state.i_w_id;
    std::vector<benchx::OrderLine> ol = state.ol;
    pending_no_states_.erase(parent_txn_id);

    benchx::NewOrderTxn new_order_txn(txn_adapter, w_id_val, d_id, c_id, o_id, datetime, i_w_id, ol);
    new_order_txn.Read();
    new_order_txn.Write();
    txn_adapter->Finialize();

    auto procedure = txn.mutable_code()->add_procedures();
    procedure->add_args("new_order");
    procedure->add_args(std::to_string(w_id_val));
    procedure->add_args(std::to_string(d_id));
    procedure->add_args(std::to_string(c_id));
    procedure->add_args(std::to_string(o_id));
    procedure->add_args(std::to_string(datetime));
    procedure->add_args(std::to_string(i_w_id));

    std::string txn_string;
    txn_string.append("new_order")
        .append(";")
        .append(std::to_string(w_id_val))
        .append(";")
        .append(std::to_string(d_id))
        .append(";")
        .append(std::to_string(c_id))
        .append(";")
        .append(std::to_string(o_id))
        .append(";")
        .append(std::to_string(datetime))
        .append(";")
        .append(std::to_string(i_w_id))
        .append(";");

    for (const auto& l : ol) {
      auto order_lines = txn.mutable_code()->add_procedures();
      order_lines->add_args(std::to_string(l.id));
      order_lines->add_args(std::to_string(l.supply_w_id));
      order_lines->add_args(std::to_string(l.item_id));
      order_lines->add_args(std::to_string(l.quantity));

      txn_string.append("new_order")
          .append(";")
          .append(std::to_string(l.id))
          .append(";")
          .append(std::to_string(l.supply_w_id))
          .append(";")
          .append(std::to_string(l.item_id))
          .append(";")
          .append(std::to_string(l.quantity))
          .append(";");
    }

    size_t char_bytes = txn_string.capacity();
    size_t string_overhead = sizeof(std::string);
    size_t total_bytes = char_bytes + string_overhead;

    avg_no_txn_size = (new_order_count * avg_no_txn_size + (float)total_bytes) / (new_order_count + 1);
    new_order_count++;
    if (!crdb_combined_mode_) return;
  }

  double skew = skew_[txn_type];

  int final_item_skewness;
  int final_cust_skewness;
  if (skew == -1.0) {
    final_item_skewness = static_cast<int>(org_item_skew * max_items_);
    if (final_item_skewness < 1) final_item_skewness = 1;
    final_cust_skewness = default_cust_skewness;
  } else {
    final_item_skewness = static_cast<int>(skew * max_items_);
    if (final_item_skewness < 1) final_item_skewness = 1;
    final_cust_skewness = static_cast<int>(skew * benchx::kCustPerDist);
    if (final_cust_skewness < 1) final_cust_skewness = 1;
  }

  auto remote_warehouses = SelectRemoteWarehouses(partition);

  int d_id = std::uniform_int_distribution<>(1, benchx::kDistPerWare)(rg_);
  int c_id = NURand(rg_, final_cust_skewness, 1, benchx::kCustPerDist);
  int o_id = id_generator_.NextOId(w_id, d_id);
  int i_w_id = w_id;
  int num_lines = std::max(1, static_cast<int>(intensity_[txn_type]));

  auto datetime = std::chrono::system_clock::now().time_since_epoch().count();
  std::vector<benchx::OrderLine> ol;
  ol.reserve(num_lines);

  std::uniform_int_distribution<> pct_dist(1, 100);
  int r = pct_dist(rg_);
  bool is_dependent = (pct_dist(rg_) <= dependent_percent_[txn_type]);

  enum class HomeType { LSH, FSH, MH };
  HomeType home_type = HomeType::LSH;
  if (r <= lsh_percent_[txn_type]) {
    home_type = HomeType::LSH;
    lsh_no++;
  } else if (r <= lsh_percent_[txn_type] + fsh_percent_[txn_type]) {
    home_type = HomeType::FSH;
    fsh_no++;
  } else {
    home_type = HomeType::MH;
    mh_no++;
  }

  if (remote_warehouses.empty()) {
    home_type = HomeType::LSH;
  }

  int remote_w_id = w_id;
  if (home_type != HomeType::LSH) {
    remote_w_id = SampleOnce(rg_, remote_warehouses);
  }

  std::uniform_int_distribution<> quantity_rnd(1, 10);
  std::set<int> unique_regions;

  for (int i = 0; i < num_lines; ++i) {
    int supply_w_id = w_id;
    if (home_type == HomeType::FSH) {
      supply_w_id = remote_w_id;
      pro.is_multi_home = true;
    } else if (home_type == HomeType::MH) {
      if (num_lines > 1) {
        if (i == 0) {
          supply_w_id = remote_w_id;
          pro.is_multi_home = true;
        } else if (i == 1) {
          supply_w_id = w_id;
        } else {
          std::bernoulli_distribution coin(0.5);
          if (coin(rg_)) {
            supply_w_id = remote_w_id;
            pro.is_multi_home = true;
          }
        }
      } else {
        supply_w_id = remote_w_id;
        pro.is_multi_home = true;
      }
    }

    ol.push_back(benchx::OrderLine{
        .id = i + 1,
        .supply_w_id = supply_w_id,
        .item_id = NURand(rg_, final_item_skewness, 1, max_items_),
        .quantity = quantity_rnd(rg_),
    });

    unique_regions.insert(GetRegionFromWarehouse(supply_w_id));
  }

  if (pro.is_multi_home) {
    // mh_no++;
  }
  if (unique_regions.size() == 1 && pro.is_multi_home) {
    pro.is_foreign_single_home = true;
    // fsh_no++;
  }

  if (is_dependent) {
    pro.dependency_type = TransactionProfile::DependencyType::FIRST_PHASE;
    pending_no_states_[pro.client_txn_id] = {w_id, d_id, o_id, i_w_id, datetime, ol};

    std::vector<int> w_ids = {w_id};
    std::vector<int> d_ids = {d_id};

    std::string string_34 =
        "CustomerName_" + std::to_string(w_id) + "_" + std::to_string(d_id) + "_" + std::to_string(c_id);
    string_34.resize(34, ' ');
    std::vector<std::string> c_names = {string_34};

    benchx::GetCustomerByNameTxn get_cust_txn(txn_adapter, w_ids, d_ids, c_names);
    get_cust_txn.Read();
    txn_adapter->Finialize();

    auto procedure = txn.mutable_code()->add_procedures();
    procedure->add_args("get_customer_by_name");
    procedure->add_args(Join(w_ids, ","));
    procedure->add_args(Join(d_ids, ","));
    procedure->add_args(Join(c_names, ","));
    if (!crdb_combined_mode_) return;
  }

  benchx::NewOrderTxn new_order_txn(txn_adapter, w_id, d_id, c_id, o_id, datetime, i_w_id, ol);
  new_order_txn.Read();
  new_order_txn.Write();
  txn_adapter->Finialize();

  auto procedure = txn.mutable_code()->add_procedures();
  procedure->add_args("new_order");
  procedure->add_args(std::to_string(w_id));
  procedure->add_args(std::to_string(d_id));
  procedure->add_args(std::to_string(c_id));
  procedure->add_args(std::to_string(o_id));
  procedure->add_args(std::to_string(datetime));
  procedure->add_args(std::to_string(i_w_id));

  std::string txn_string;
  txn_string.append("new_order")
      .append(";")
      .append(std::to_string(w_id))
      .append(";")
      .append(std::to_string(d_id))
      .append(";")
      .append(std::to_string(c_id))
      .append(";")
      .append(std::to_string(o_id))
      .append(";")
      .append(std::to_string(datetime))
      .append(";")
      .append(std::to_string(i_w_id))
      .append(";");

  for (const auto& l : ol) {
    auto order_lines = txn.mutable_code()->add_procedures();
    order_lines->add_args(std::to_string(l.id));
    order_lines->add_args(std::to_string(l.supply_w_id));
    order_lines->add_args(std::to_string(l.item_id));
    order_lines->add_args(std::to_string(l.quantity));

    txn_string.append("new_order")
        .append(";")
        .append(std::to_string(l.id))
        .append(";")
        .append(std::to_string(l.supply_w_id))
        .append(";")
        .append(std::to_string(l.item_id))
        .append(";")
        .append(std::to_string(l.quantity))
        .append(";");
  }

  size_t char_bytes = txn_string.capacity();
  size_t string_overhead = sizeof(std::string);
  size_t total_bytes = char_bytes + string_overhead;

  avg_no_txn_size = (new_order_count * avg_no_txn_size + (float)total_bytes) / (new_order_count + 1);
  new_order_count++;
}

// The InsertOnly transaction will insert a set of rows into the history table
// For that we need to populate the columns:
// H_C_ID → Customer ID
// H_C_D_ID → District of the customer
// H_C_W_ID → Warehouse of the customer
// H_D_ID → District receiving the payment
// H_W_ID → Warehouse receiving the payment
// H_DATE → Timestamp of payment
// H_AMOUNT → Amount paid
// H_DATA → Free-text note (often warehouse + district name)
void BenchXWorkload::InsertOnly(Transaction& txn, TransactionProfile& pro, int w_id, int partition, int txn_type) {
  auto txn_adapter = std::make_shared<benchx::TxnKeyGenStorageAdapter>(txn);

  if (prev_txn_ != nullptr && prev_profile_ != nullptr) {
    TxnId parent_txn_id = prev_profile_->client_txn_id;
    auto& state = pending_io_states_[parent_txn_id];

    int num_reads = prev_txn_->keys_size();
    std::vector<int> c_ids;
    size_t expected_size = state.w_ids.size();
    for (size_t i = 0; i < expected_size; i++) {
      int c_id;
      int key_idx = (i < num_reads) ? i : (num_reads - 1);
      std::memcpy(&c_id, prev_txn_->keys(key_idx).value_entry().value().data() + 39, sizeof(int));
      c_ids.push_back(c_id);
    }
    prev_txn_ = nullptr;
    pro.dependency_type = TransactionProfile::DependencyType::SECOND_PHASE;

    std::vector<int> w_ids = state.w_ids;
    std::vector<int> d_ids = state.d_ids;
    std::vector<int> c_w_ids = state.c_w_ids;
    std::vector<int> c_d_ids = state.c_d_ids;
    std::vector<int64_t> amounts = state.amounts;
    std::vector<int64_t> datetimes = state.datetimes;
    std::vector<int> h_ids = state.h_ids;
    pending_io_states_.erase(parent_txn_id);

    benchx::InsertTxn insert_txn(txn_adapter, w_ids, d_ids, c_w_ids, c_d_ids, c_ids, amounts, datetimes, h_ids);
    insert_txn.Read();
    insert_txn.Write();
    txn_adapter->Finialize();

    auto procedure = txn.mutable_code()->add_procedures();
    procedure->add_args("insert_only");
    procedure->add_args(Join(w_ids, ","));
    procedure->add_args(Join(d_ids, ","));
    procedure->add_args(Join(c_w_ids, ","));
    procedure->add_args(Join(c_d_ids, ","));
    procedure->add_args(Join(c_ids, ","));
    procedure->add_args(Join(amounts, ","));
    procedure->add_args(Join(datetimes, ","));
    procedure->add_args(Join(h_ids, ","));

    insert_only_count++;
    if (!crdb_combined_mode_) return;
  }

  // LOG(INFO) << "Generating InsertOnly transaction for warehouse " << w_id << " in partition " << partition;
  double skew = skew_[txn_type];
  int final_cust_skew;
  if (skew == -1.0) {
    final_cust_skew = org_cust_skew;
  } else {
    final_cust_skew = skew * benchx::kCustPerDist;
  }

  auto remote_warehouses = SelectRemoteWarehouses(partition);
  std::uniform_int_distribution<> d_id_rnd(1, benchx::kDistPerWare);
  auto datetime = std::chrono::system_clock::now().time_since_epoch().count();
  std::uniform_int_distribution<> pct_dist(1, 100);
  int r = pct_dist(rg_);
  bool is_dependent = (pct_dist(rg_) <= dependent_percent_[txn_type]);

  enum class HomeType { LSH, FSH, MH };
  HomeType home_type = HomeType::LSH;
  if (r <= lsh_percent_[txn_type]) {
    home_type = HomeType::LSH;
    lsh_io++;
  } else if (r <= lsh_percent_[txn_type] + fsh_percent_[txn_type]) {
    home_type = HomeType::FSH;
    fsh_io++;
  } else {
    home_type = HomeType::MH;
    mh_io++;
  }

  if (remote_warehouses.empty()) {
    home_type = HomeType::LSH;
  }

  int remote_w_id = w_id;
  if (home_type != HomeType::LSH) {
    remote_w_id = SampleOnce(rg_, remote_warehouses);
  }

  int num_inserts = std::max(1, static_cast<int>(intensity_[txn_type]));  // Use intensity value
  std::vector<int> w_ids, d_ids, c_w_ids, c_d_ids, c_ids, h_ids;
  std::vector<int64_t> amounts, datetimes;

  for (int i = 0; i < num_inserts; ++i) {
    int c_id = NURand(rg_, final_cust_skew, 1, benchx::kCustPerDist);
    auto h_id = id_generator_.NextHId(w_id, d_id_rnd(rg_));  // Generate unique h_id per insert
    auto amount = std::uniform_int_distribution<int64_t>(100, 500000)(rg_);
    auto dt = datetime + i;  // Slight variation in datetime for each insert
    int c_w_id = w_id;
    int c_d_id = d_id_rnd(rg_);

    if (home_type == HomeType::FSH) {
      c_w_id = remote_w_id;
      pro.is_multi_home = true;
      pro.is_foreign_single_home = true;
    } else if (home_type == HomeType::MH) {
      if (num_inserts > 1) {
        if (i == 0) {
          c_w_id = remote_w_id;
          pro.is_multi_home = true;
        } else if (i == 1) {
          c_w_id = w_id;
        } else {
          std::bernoulli_distribution coin(0.5);
          if (coin(rg_)) {
            c_w_id = remote_w_id;
            pro.is_multi_home = true;
          }
        }
      } else {
        c_w_id = remote_w_id;
        pro.is_multi_home = true;
        pro.is_foreign_single_home = true;
      }
    }

    w_ids.push_back(w_id);
    d_ids.push_back(d_id_rnd(rg_));  // Vary d_id per insert
    c_w_ids.push_back(c_w_id);
    c_d_ids.push_back(c_d_id);
    c_ids.push_back(c_id);
    amounts.push_back(amount);
    datetimes.push_back(dt);
    h_ids.push_back(h_id);
  }

  if (is_dependent) {
    pro.dependency_type = TransactionProfile::DependencyType::FIRST_PHASE;
    pending_io_states_[pro.client_txn_id] = {w_ids, d_ids, c_w_ids, c_d_ids, amounts, datetimes, h_ids};

    std::vector<std::string> c_names;
    for (size_t i = 0; i < c_ids.size(); i++) {
      std::string string_34 = "CustomerName_" + std::to_string(c_w_ids[i]) + "_" + std::to_string(c_d_ids[i]) + "_" +
                              std::to_string(c_ids[i]);
      string_34.resize(34, ' ');
      c_names.push_back(string_34);
    }

    benchx::GetCustomerByNameTxn get_cust_txn(txn_adapter, c_w_ids, c_d_ids, c_names);
    get_cust_txn.Read();
    txn_adapter->Finialize();

    auto procedure = txn.mutable_code()->add_procedures();
    procedure->add_args("get_customer_by_name");
    procedure->add_args(Join(c_w_ids, ","));
    procedure->add_args(Join(c_d_ids, ","));
    procedure->add_args(Join(c_names, ","));
    if (!crdb_combined_mode_) return;
  }

  benchx::InsertTxn insert_txn(txn_adapter, w_ids, d_ids, c_w_ids, c_d_ids, c_ids, amounts, datetimes, h_ids);
  insert_txn.Read();
  insert_txn.Write();
  txn_adapter->Finialize();
  // LOG(INFO) << "Generated InsertTxn object";

  auto procedure = txn.mutable_code()->add_procedures();
  procedure->add_args("insert_only");
  procedure->add_args(Join(w_ids, ","));
  procedure->add_args(Join(d_ids, ","));
  procedure->add_args(Join(c_w_ids, ","));
  procedure->add_args(Join(c_d_ids, ","));
  procedure->add_args(Join(c_ids, ","));
  procedure->add_args(Join(amounts, ","));
  procedure->add_args(Join(datetimes, ","));
  procedure->add_args(Join(h_ids, ","));

  // For workload profiling purposes combine txn into a single string
  std::string txn_string;
  txn_string.append("insert_only")
      .append(";")
      .append(Join(w_ids, ","))
      .append(";")
      .append(Join(d_ids, ","))
      .append(";")
      .append(Join(c_w_ids, ","))
      .append(";")
      .append(Join(c_d_ids, ","))
      .append(";")
      .append(Join(c_ids, ","))
      .append(";")
      .append(Join(amounts, ","))
      .append(";")
      .append(Join(datetimes, ","))
      .append(";")
      .append(Join(h_ids, ","))
      .append(";");

  // Analytically calculate the size of txn_string for the memory footprint
  size_t char_bytes = txn_string.capacity();     // allocated chars (includes unused)
  size_t string_overhead = sizeof(std::string);  // object overhead (on stack)
  size_t total_bytes = char_bytes + string_overhead;

  avg_io_txn_size = (insert_only_count * avg_io_txn_size + (float)total_bytes) / (insert_only_count + 1);
  insert_only_count++;
  // LOG(INFO) << "Finalized InsertOnly transaction generation";
}

void BenchXWorkload::OrderStatus(Transaction& txn, TransactionProfile& pro, int w_id, int partition, int txn_type) {
  auto txn_adapter = std::make_shared<benchx::TxnKeyGenStorageAdapter>(txn);

  double skew = skew_[txn_type];
  int final_cust_skew;
  if (skew == -1.0) {
    final_cust_skew = org_cust_skew;
  } else {
    final_cust_skew = skew * benchx::kCustPerDist;
  }

  if (prev_txn_ != nullptr && prev_profile_ != nullptr) {
    TxnId parent_txn_id = prev_profile_->client_txn_id;
    auto& state = pending_os_states_[parent_txn_id];

    int num_reads = prev_txn_->keys_size();
    std::vector<int> c_ids;
    size_t expected_size = state.w_ids.size();
    for (size_t i = 0; i < expected_size; i++) {
      int c_id;
      int key_idx = (i < num_reads) ? i : (num_reads - 1);
      std::memcpy(&c_id, prev_txn_->keys(key_idx).value_entry().value().data() + 39, sizeof(int));
      c_ids.push_back(c_id);
    }
    prev_txn_ = nullptr;
    pro.dependency_type = TransactionProfile::DependencyType::SECOND_PHASE;

    std::vector<int> w_ids = state.w_ids;
    std::vector<int> d_ids = state.d_ids;
    std::vector<int> o_ids = state.o_ids;
    pending_os_states_.erase(parent_txn_id);

    benchx::OrderStatusTxn order_status_txn(txn_adapter, w_ids, d_ids, c_ids, o_ids, intensity_[txn_type]);
    order_status_txn.Read();
    txn_adapter->Finialize();

    auto procedure = txn.mutable_code()->add_procedures();
    procedure->add_args("order_status");
    procedure->add_args(Join(w_ids, ","));
    procedure->add_args(Join(d_ids, ","));
    procedure->add_args(Join(c_ids, ","));
    procedure->add_args(Join(o_ids, ","));

    order_status_count++;
    if (!crdb_combined_mode_) return;
  }

  int num_reads = std::max(1, static_cast<int>(intensity_[txn_type]));
  std::uniform_int_distribution<> d_id_rnd(1, benchx::kDistPerWare);
  auto max_o_id = id_generator_.max_o_id();

  std::uniform_int_distribution<> pct_dist(1, 100);
  int r = pct_dist(rg_);
  bool is_dependent = (pct_dist(rg_) <= dependent_percent_[txn_type]);

  enum class HomeType { LSH, FSH, MH };
  HomeType home_type = HomeType::LSH;
  if (r <= lsh_percent_[txn_type]) {
    home_type = HomeType::LSH;
    lsh_os++;
  } else if (r <= lsh_percent_[txn_type] + fsh_percent_[txn_type]) {
    home_type = HomeType::FSH;
    fsh_os++;
  } else {
    home_type = HomeType::MH;
    mh_os++;
  }

  auto remote_warehouses = SelectRemoteWarehouses(partition);
  if (remote_warehouses.empty()) {
    home_type = HomeType::LSH;
  }

  int remote_w_id = w_id;
  if (home_type != HomeType::LSH) {
    remote_w_id = SampleOnce(rg_, remote_warehouses);
  }

  std::vector<int> w_ids, d_ids, c_ids, o_ids;
  w_ids.reserve(num_reads);
  d_ids.reserve(num_reads);
  c_ids.reserve(num_reads);
  o_ids.reserve(num_reads);

  for (int i = 0; i < num_reads; ++i) {
    int d_id = d_id_rnd(rg_);
    double final_cust_skew = skew_[txn_type];
    int c_id = NURand(rg_, final_cust_skew, 1, benchx::kCustPerDist);
    int o_id = std::uniform_int_distribution<>(std::max(1, max_o_id - 5), max_o_id)(rg_);
    int read_w_id = w_id;

    if (home_type == HomeType::FSH) {
      read_w_id = remote_w_id;
      pro.is_multi_home = true;
      pro.is_foreign_single_home = true;
    } else if (home_type == HomeType::MH) {
      if (num_reads > 1) {
        if (i == 0) {
          read_w_id = remote_w_id;
          pro.is_multi_home = true;
        } else if (i == 1) {
          read_w_id = w_id;
        } else {
          std::bernoulli_distribution coin(0.5);
          if (coin(rg_)) {
            read_w_id = remote_w_id;
            pro.is_multi_home = true;
          }
        }
      } else {
        read_w_id = remote_w_id;
        pro.is_multi_home = true;
        pro.is_foreign_single_home = true;
      }
    }

    w_ids.push_back(read_w_id);
    d_ids.push_back(d_id);
    c_ids.push_back(c_id);
    o_ids.push_back(o_id);
  }

  if (is_dependent) {
    pro.dependency_type = TransactionProfile::DependencyType::FIRST_PHASE;
    pending_os_states_[pro.client_txn_id] = {w_ids, d_ids, c_ids, o_ids};

    std::vector<std::string> c_names;
    for (size_t i = 0; i < c_ids.size(); i++) {
      std::string string_34 =
          "CustomerName_" + std::to_string(w_ids[i]) + "_" + std::to_string(d_ids[i]) + "_" + std::to_string(c_ids[i]);
      string_34.resize(34, ' ');
      c_names.push_back(string_34);
    }

    benchx::GetCustomerByNameTxn get_cust_txn(txn_adapter, w_ids, d_ids, c_names);
    get_cust_txn.Read();
    txn_adapter->Finialize();

    auto procedure = txn.mutable_code()->add_procedures();
    procedure->add_args("get_customer_by_name");
    procedure->add_args(Join(w_ids, ","));
    procedure->add_args(Join(d_ids, ","));
    procedure->add_args(Join(c_names, ","));
    if (!crdb_combined_mode_) return;
  }

  benchx::OrderStatusTxn order_status_txn(txn_adapter, w_ids, d_ids, c_ids, o_ids, intensity_[txn_type]);
  order_status_txn.Read();
  txn_adapter->Finialize();
  // LOG(INFO) << "Generated OrderStatusTxn object";

  auto procedure = txn.mutable_code()->add_procedures();
  procedure->add_args("order_status");
  procedure->add_args(Join(w_ids, ","));
  procedure->add_args(Join(d_ids, ","));
  procedure->add_args(Join(c_ids, ","));
  procedure->add_args(Join(o_ids, ","));

  std::string txn_string;
  txn_string.append("order_status")
      .append(";")
      .append(Join(w_ids, ","))
      .append(";")
      .append(Join(d_ids, ","))
      .append(";")
      .append(Join(c_ids, ","))
      .append(";")
      .append(Join(o_ids, ","))
      .append(";");

  size_t char_bytes = txn_string.capacity();
  size_t string_overhead = sizeof(std::string);
  size_t total_bytes = char_bytes + string_overhead;

  avg_os_txn_size = (order_status_count * avg_os_txn_size + (float)total_bytes) / (order_status_count + 1);
  order_status_count++;
}

void BenchXWorkload::DeleteOnly(Transaction& txn, TransactionProfile& pro, int w_id, int partition, int txn_type) {
  auto txn_adapter = std::make_shared<benchx::TxnKeyGenStorageAdapter>(txn);

  if (prev_txn_ != nullptr && prev_profile_ != nullptr) {
    TxnId parent_txn_id = prev_profile_->client_txn_id;
    auto& state = pending_del_states_[parent_txn_id];

    int num_reads = prev_txn_->keys_size();
    std::vector<int> c_ids;
    size_t expected_size = state.w_ids.size();
    for (size_t i = 0; i < expected_size; i++) {
      int c_id;
      int key_idx = (i < num_reads) ? i : (num_reads - 1);
      std::memcpy(&c_id, prev_txn_->keys(key_idx).value_entry().value().data() + 39, sizeof(int));
      c_ids.push_back(c_id);
    }
    prev_txn_ = nullptr;
    pro.dependency_type = TransactionProfile::DependencyType::SECOND_PHASE;

    std::vector<int> w_ids = state.w_ids;
    std::vector<int> d_ids = state.d_ids;
    std::vector<int> c_w_ids = state.c_w_ids;
    std::vector<int> c_d_ids = state.c_d_ids;
    std::vector<int> h_ids = state.h_ids;
    pending_del_states_.erase(parent_txn_id);

    benchx::DeleteTxn delete_txn(txn_adapter, w_ids, d_ids, c_w_ids, c_d_ids, c_ids, h_ids);
    delete_txn.Read();
    delete_txn.Write();
    txn_adapter->Finialize();

    auto procedure = txn.mutable_code()->add_procedures();
    procedure->add_args("delete_only");
    procedure->add_args(Join(w_ids, ","));
    procedure->add_args(Join(d_ids, ","));
    procedure->add_args(Join(c_w_ids, ","));
    procedure->add_args(Join(c_d_ids, ","));
    procedure->add_args(Join(c_ids, ","));
    procedure->add_args(Join(h_ids, ","));

    delete_only_count++;
    if (!crdb_combined_mode_) return;
  }

  double skew = skew_[txn_type];
  int final_cust_skew;
  if (skew == -1.0) {
    final_cust_skew = org_cust_skew;
  } else {
    final_cust_skew = skew * benchx::kCustPerDist;
  }

  std::uniform_int_distribution<> pct_dist(1, 100);
  int r = pct_dist(rg_);
  bool is_dependent = (pct_dist(rg_) <= dependent_percent_[txn_type]);

  enum class HomeType { LSH, FSH, MH };
  HomeType home_type = HomeType::LSH;
  if (r <= lsh_percent_[txn_type]) {
    home_type = HomeType::LSH;
    lsh_del++;
  } else if (r <= lsh_percent_[txn_type] + fsh_percent_[txn_type]) {
    home_type = HomeType::FSH;
    fsh_del++;
  } else {
    home_type = HomeType::MH;
    mh_del++;
  }

  auto remote_warehouses = SelectRemoteWarehouses(partition);
  if (remote_warehouses.empty()) {
    home_type = HomeType::LSH;
  }

  int remote_w_id = w_id;
  if (home_type != HomeType::LSH) {
    remote_w_id = SampleOnce(rg_, remote_warehouses);
  }

  int num_deletes = std::max(1, static_cast<int>(intensity_[txn_type]));  // Use intensity value
  std::uniform_int_distribution<> d_id_rnd(1, benchx::kDistPerWare);

  std::vector<int> w_ids, d_ids, c_w_ids, c_d_ids, c_ids, h_ids;
  w_ids.reserve(num_deletes);
  d_ids.reserve(num_deletes);
  c_w_ids.reserve(num_deletes);
  c_d_ids.reserve(num_deletes);
  c_ids.reserve(num_deletes);
  h_ids.reserve(num_deletes);

  for (int i = 0; i < num_deletes; ++i) {
    int d_id = d_id_rnd(rg_);
    int c_id = NURand(rg_, final_cust_skew, 1, benchx::kCustPerDist);
    int c_w_id = w_id;
    int c_d_id = d_id;
    int h_id = 1;  // All history rows in load_tables have ID=1

    if (home_type == HomeType::FSH) {
      c_w_id = remote_w_id;
      pro.is_multi_home = true;
      pro.is_foreign_single_home = true;
    } else if (home_type == HomeType::MH) {
      if (num_deletes > 1) {
        if (i == 0) {
          c_w_id = remote_w_id;
          pro.is_multi_home = true;
        } else if (i == 1) {
          c_w_id = w_id;
        } else {
          std::bernoulli_distribution coin(0.5);
          if (coin(rg_)) {
            c_w_id = remote_w_id;
            pro.is_multi_home = true;
          }
        }
      } else {
        c_w_id = remote_w_id;
        pro.is_multi_home = true;
        pro.is_foreign_single_home = true;
      }
    }

    w_ids.push_back(w_id);
    d_ids.push_back(d_id);
    c_w_ids.push_back(c_w_id);
    c_d_ids.push_back(c_d_id);
    c_ids.push_back(c_id);
    h_ids.push_back(h_id);
  }

  if (is_dependent) {
    pro.dependency_type = TransactionProfile::DependencyType::FIRST_PHASE;
    pending_del_states_[pro.client_txn_id] = {w_ids, d_ids, c_w_ids, c_d_ids, h_ids};

    std::vector<std::string> c_names;
    for (size_t i = 0; i < c_ids.size(); i++) {
      std::string string_34 = "CustomerName_" + std::to_string(c_w_ids[i]) + "_" + std::to_string(c_d_ids[i]) + "_" +
                              std::to_string(c_ids[i]);
      string_34.resize(34, ' ');
      c_names.push_back(string_34);
    }

    benchx::GetCustomerByNameTxn get_cust_txn(txn_adapter, c_w_ids, c_d_ids, c_names);
    get_cust_txn.Read();
    txn_adapter->Finialize();

    auto procedure = txn.mutable_code()->add_procedures();
    procedure->add_args("get_customer_by_name");
    procedure->add_args(Join(c_w_ids, ","));
    procedure->add_args(Join(c_d_ids, ","));
    procedure->add_args(Join(c_names, ","));
    if (!crdb_combined_mode_) return;
  }

  benchx::DeleteTxn delete_txn(txn_adapter, w_ids, d_ids, c_w_ids, c_d_ids, c_ids, h_ids);
  delete_txn.Read();
  delete_txn.Write();
  txn_adapter->Finialize();

  auto procedure = txn.mutable_code()->add_procedures();
  procedure->add_args("delete_only");
  procedure->add_args(Join(w_ids, ","));
  procedure->add_args(Join(d_ids, ","));
  procedure->add_args(Join(c_w_ids, ","));
  procedure->add_args(Join(c_d_ids, ","));
  procedure->add_args(Join(c_ids, ","));
  procedure->add_args(Join(h_ids, ","));

  std::string txn_string;
  txn_string.append("delete_only")
      .append(";")
      .append(Join(w_ids, ","))
      .append(";")
      .append(Join(d_ids, ","))
      .append(";")
      .append(Join(c_w_ids, ","))
      .append(";")
      .append(Join(c_d_ids, ","))
      .append(";")
      .append(Join(c_ids, ","))
      .append(";")
      .append(Join(h_ids, ","))
      .append(";");

  size_t char_bytes = txn_string.capacity();
  size_t string_overhead = sizeof(std::string);
  size_t total_bytes = char_bytes + string_overhead;

  avg_del_txn_size = (delete_only_count * avg_del_txn_size + (float)total_bytes) / (delete_only_count + 1);
  delete_only_count++;
}

void BenchXWorkload::StockLevel(Transaction& txn, TransactionProfile& pro, int w_id, int partition, int txn_type) {
  auto txn_adapter = std::make_shared<benchx::TxnKeyGenStorageAdapter>(txn);

  if (prev_txn_ != nullptr && prev_profile_ != nullptr) {
    std::array<int, benchx::StockLevelTxn::kTotalItems> i_ids;
    int num_keys = prev_txn_->keys_size();
    for (size_t i = 0; i < benchx::StockLevelTxn::kTotalItems; i++) {
      int i_id;
      int key_idx = (i < num_keys) ? i : (num_keys - 1);
      std::memcpy(&i_id, prev_txn_->keys(key_idx).value_entry().value().data() + 28, sizeof(int));
      i_ids[i] = i_id;
    }
    prev_txn_ = nullptr;
    pro.dependency_type = TransactionProfile::DependencyType::SECOND_PHASE;

    TxnId parent_txn_id = prev_profile_->client_txn_id;
    auto& state = pending_sl_states_[parent_txn_id];
    std::vector<int> w_ids = state.w_ids;
    std::vector<int> d_ids = state.d_ids;
    std::vector<int> o_ids = state.o_ids;
    pending_sl_states_.erase(parent_txn_id);

    benchx::StockLevelTxn stock_level(txn_adapter, w_ids, d_ids, o_ids, i_ids);
    stock_level.Read();
    txn_adapter->Finialize();

    auto procedure = txn.mutable_code()->add_procedures();
    procedure->add_args("stock_level");
    procedure->add_args(Join(w_ids, ","));
    procedure->add_args(Join(d_ids, ","));
    procedure->add_args(Join(o_ids, ","));

    std::string txn_string;
    txn_string.append("stock_level")
        .append(";")
        .append(Join(w_ids, ","))
        .append(";")
        .append(Join(d_ids, ","))
        .append(";")
        .append(Join(o_ids, ","))
        .append(";");

    auto items = txn.mutable_code()->add_procedures();
    for (auto i_id : i_ids) {
      items->add_args(std::to_string(i_id));
      txn_string.append(std::to_string(i_id)).append(";");
    }

    size_t char_bytes = txn_string.capacity();
    size_t string_overhead = sizeof(std::string);
    size_t total_bytes = char_bytes + string_overhead;

    avg_sl_txn_size = (stock_level_count * avg_sl_txn_size + (float)total_bytes) / (stock_level_count + 1);
    stock_level_count++;
    if (!crdb_combined_mode_) return;
  }

  std::uniform_int_distribution<> pct_dist(1, 100);
  int r = pct_dist(rg_);
  bool is_dependent = (pct_dist(rg_) <= dependent_percent_[txn_type]);

  enum class HomeType { LSH, FSH, MH };
  HomeType home_type = HomeType::LSH;
  if (r <= lsh_percent_[txn_type]) {
    home_type = HomeType::LSH;
    lsh_sl++;
  } else if (r <= lsh_percent_[txn_type] + fsh_percent_[txn_type]) {
    home_type = HomeType::FSH;
    fsh_sl++;
  } else {
    home_type = HomeType::MH;
    mh_sl++;
  }

  auto remote_warehouses = SelectRemoteWarehouses(partition);
  if (remote_warehouses.empty()) {
    home_type = HomeType::LSH;
  }

  double skew = skew_[txn_type];
  int final_warehouse_skew = skew == -1.0 ? static_cast<int>(0.341 * remote_warehouses.size())
                                          : static_cast<int>(skew * remote_warehouses.size());
  int final_item_skew = skew == -1.0 ? static_cast<int>(org_item_skew * max_items_) : static_cast<int>(skew * max_items_);
  if (final_warehouse_skew < 1) final_warehouse_skew = 1;
  if (final_item_skew < 1) final_item_skew = 1;

  int remote_w_id = w_id;
  if (home_type != HomeType::LSH && !remote_warehouses.empty()) {
    int w_idx = NURand(rg_, final_warehouse_skew, 0, remote_warehouses.size() - 1);
    remote_w_id = remote_warehouses[w_idx];
  }

  int num_scans = std::max(1, static_cast<int>(intensity_[txn_type]));
  std::uniform_int_distribution<> d_id_rnd(1, benchx::kDistPerWare);
  auto max_o_id = id_generator_.max_o_id();

  std::vector<int> w_ids, d_ids, o_ids;
  w_ids.reserve(num_scans);
  d_ids.reserve(num_scans);
  o_ids.reserve(num_scans);

  for (int i = 0; i < num_scans; ++i) {
    int d_id = d_id_rnd(rg_);
    int o_id = max_o_id;  // Using exact max_o_id similar to original logic
    int scan_w_id = w_id;

    if (home_type == HomeType::FSH) {
      scan_w_id = remote_w_id;
      pro.is_multi_home = true;
      pro.is_foreign_single_home = true;
    } else if (home_type == HomeType::MH) {
      if (num_scans > 1) {
        if (i == 0) {
          scan_w_id = remote_w_id;
          pro.is_multi_home = true;
        } else if (i == 1) {
          scan_w_id = w_id;
        } else {
          std::bernoulli_distribution coin(0.5);
          if (coin(rg_)) {
            scan_w_id = remote_w_id;
            pro.is_multi_home = true;
          }
        }
      } else {
        scan_w_id = remote_w_id;
        pro.is_multi_home = true;
        pro.is_foreign_single_home = true;
      }
    }

    w_ids.push_back(scan_w_id);
    d_ids.push_back(d_id);
    o_ids.push_back(o_id);
  }

  std::array<int, benchx::StockLevelTxn::kTotalItems> i_ids;
  for (size_t i = 0; i < i_ids.size(); i++) {
    i_ids[i] = NURand(rg_, final_item_skew, 1, max_items_);
  }

  if (is_dependent) {
    pro.dependency_type = TransactionProfile::DependencyType::FIRST_PHASE;
    pending_sl_states_[pro.client_txn_id] = {w_ids, d_ids, o_ids};

    int region = GetRegionFromWarehouse(w_id);
    int rep_w = partition + 1 + region * config_->num_partitions();

    std::vector<int> lookup_w_ids;
    std::vector<std::string> i_names;
    for (size_t i = 0; i < benchx::StockLevelTxn::kTotalItems; i++) {
      lookup_w_ids.push_back(rep_w);  // use representative warehouse!
      std::string name = "ItemName_" + std::to_string(i_ids[i]);
      name.resize(24, ' ');
      i_names.push_back(name);
    }

    benchx::GetItemByNameTxn get_item_txn(txn_adapter, lookup_w_ids, i_names);
    get_item_txn.Read();
    txn_adapter->Finialize();

    auto procedure = txn.mutable_code()->add_procedures();
    procedure->add_args("get_item_by_name");
    procedure->add_args(Join(lookup_w_ids, ","));
    procedure->add_args(Join(i_names, ","));
    if (!crdb_combined_mode_) return;
  }

  benchx::StockLevelTxn stock_level(txn_adapter, w_ids, d_ids, o_ids, i_ids);
  stock_level.Read();
  txn_adapter->Finialize();

  auto procedure = txn.mutable_code()->add_procedures();
  procedure->add_args("stock_level");
  procedure->add_args(Join(w_ids, ","));
  procedure->add_args(Join(d_ids, ","));
  procedure->add_args(Join(o_ids, ","));

  std::string txn_string;
  txn_string.append("stock_level")
      .append(";")
      .append(Join(w_ids, ","))
      .append(";")
      .append(Join(d_ids, ","))
      .append(";")
      .append(Join(o_ids, ","))
      .append(";");

  auto items = txn.mutable_code()->add_procedures();
  for (auto i_id : i_ids) {
    items->add_args(to_string(i_id));
    txn_string.append(to_string(i_id)).append(";");
  }

  // Analytically calculate the size of txn_string for the memory footprint
  size_t char_bytes = txn_string.capacity();     // allocated chars (includes unused)
  size_t string_overhead = sizeof(std::string);  // object overhead (on stack)
  size_t total_bytes = char_bytes + string_overhead;

  avg_sl_txn_size = (stock_level_count * avg_sl_txn_size + (float)total_bytes) / (stock_level_count + 1);
  stock_level_count++;
}

std::vector<int> BenchXWorkload::SelectRemoteWarehouses(int partition) {
  if (params_.GetInt32(SH_ONLY) == 1) {
    return {SampleOnce(rg_, warehouse_index_[partition][local_region()])};
  }

  auto num_regions = GetNumRegions(config_);
  auto max_num_homes = std::min(params_.GetInt32(HOMES), num_regions);
  if (max_num_homes < 2) {
    return {};
  }
  auto num_homes = std::uniform_int_distribution{2, max_num_homes}(rg_);
  auto remote_warehouses = zipf_sample(rg_, zipf_coef_, distance_ranking_, num_homes - 1);

  for (size_t i = 0; i < remote_warehouses.size(); i++) {
    auto r = remote_warehouses[i];
    remote_warehouses[i] = SampleOnce(rg_, warehouse_index_[partition][r]);
  }

  return remote_warehouses;
}

int BenchXWorkload::GetRegionFromWarehouse(int warehouse_id) {
  auto num_regions = config_->num_regions();
  int region = ((warehouse_id - 1) / config_->num_partitions()) % num_regions;
  return region;
}

}  // namespace slog
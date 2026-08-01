#include <gflags/gflags.h>
#include <glog/logging.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <pqxx/pqxx>
#include <string>
#include <thread>
#include <vector>

#include "common/configuration.h"
#include "crdb/crdb_benchx_sql_translator.h"
#include "workload/benchx.h"

using namespace std;
using namespace slog;

const int CRDB_PORT = 26257;

struct TransactionRecord {
  std::string code;
  long long sent_at;
  long long received_at;
  bool is_multi_home;
  bool is_multi_partition;
  long restarts;
};

struct Stats {
  std::vector<TransactionRecord> transactions;
  long committed = 0;
  long aborted = 0;
  long total_restarts = 0;
  long multi_home = 0;
  long multi_partition = 0;
  long single_partition = 0;
};

// Global config
std::shared_ptr<slog::Configuration> config;

void worker(int id, std::string conn_str, string params_str, int nregs, int client_reg, atomic<bool>& running,
            Stats& stats, uint32_t base_seed) {
  try {
    pqxx::connection c{conn_str};

    // Initialize BenchXWorkload for this thread
    BenchXWorkload workload(config, client_reg, client_reg, params_str, {id + 1, 100}, base_seed + id);
    workload.SetCRDBCombinedMode(true);

    while (running) {
      auto [txn, profile] = workload.NextTransaction();
      if (!txn) {
        continue;
      }

      auto sent_at = chrono::duration_cast<chrono::nanoseconds>(chrono::system_clock::now().time_since_epoch()).count();

      bool success = false;
      try {
        pqxx::work pq_txn{c};
        TranslateBenchXToSQL(pq_txn, *txn);
        pq_txn.commit();
        success = true;
      } catch (const pqxx::serialization_failure& e) {
        // CockroachDB serialization failure (retry)
        stats.total_restarts++;
        success = false;
      } catch (const std::exception& e) {
        LOG(ERROR) << "Thread " << id << " error: " << e.what();
        success = false;
      }

      auto received_at =
          chrono::duration_cast<chrono::nanoseconds>(chrono::system_clock::now().time_since_epoch()).count();

      if (success) {
        stats.committed++;

        // Estimate MH / MP based on profile
        if (profile.is_multi_home)
          stats.multi_home++;
        else if (profile.is_multi_partition)
          stats.multi_partition++;
        else
          stats.single_partition++;

        std::string code_str = "";
        int num_procs = txn->code().procedures_size();
        if (num_procs > 0) {
          int main_proc_idx = 0;
          if (num_procs > 1 && txn->code().procedures(0).args_size() > 0 &&
              (txn->code().procedures(0).args(0) == "get_customer_by_name" ||
               txn->code().procedures(0).args(0) == "get_item_by_name")) {
            main_proc_idx = 1;
          }

          int code_length = txn->code().procedures(main_proc_idx).args_size();
          for (int i = 0; i < code_length; i++) {
            std::string arg = txn->code().procedures(main_proc_idx).args(i);
            std::replace(arg.begin(), arg.end(), ',', ';');
            code_str += arg;
            if (i != code_length - 1) code_str += ";";
          }

          if (main_proc_idx == 1) {
            int first_proc_len = txn->code().procedures(0).args_size();
            for (int i = 0; i < first_proc_len; i++) {
              std::string arg = txn->code().procedures(0).args(i);
              if (arg.length() > 4 && arg.substr(0, 4) == "dep_") {
                code_str += ";" + arg;
                break;
              }
            }
          }
        }
        stats.transactions.push_back({code_str, (long long)sent_at, (long long)received_at, profile.is_multi_home, profile.is_multi_partition, stats.total_restarts});
      } else {
        stats.aborted++;
      }

      delete txn;
    }
  } catch (const exception& e) {
    LOG(ERROR) << "Worker " << id << " failed to connect or crashed: " << e.what();
  }
}

int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;

  if (argc < 9) {
    LOG(ERROR) << "Usage: ./benchmark_benchx_crdb <IP> <threads> <duration> <num_parts> <num_regs> <client_reg> "
                  "<params> <seed>";
    return 1;
  }

  string ip = argv[1];
  int threads = stoi(argv[2]);
  int duration = stoi(argv[3]);
  int num_parts = stoi(argv[4]);
  int num_regs = stoi(argv[5]);
  int client_reg = stoi(argv[6]);
  string params = argv[7];
  uint32_t seed = stoul(argv[8]);

  // Extract warehouses from params if present, else default to 1200
  int total_warehouses = 1200;
  size_t pos = params.find("warehouses=");
  if (pos != string::npos) {
    size_t end_pos = params.find(";", pos);
    if (end_pos == string::npos) end_pos = params.length();
    total_warehouses = stoi(params.substr(pos + 11, end_pos - pos - 11));
    // Remove the warehouses parameter from the string so BenchXWorkload doesn't complain
    if (end_pos < params.length() && params[end_pos] == ';') end_pos++;
    params.erase(pos, end_pos - pos);
  }

  // Create a dummy Detock configuration
  slog::internal::Configuration config_proto;
  config_proto.set_num_partitions(num_parts);
  config_proto.add_broker_ports(1000);
  config_proto.set_server_port(1001);
  config_proto.set_forwarder_port(1002);
  config_proto.set_sequencer_port(1003);
  config_proto.set_execution_type(slog::internal::ExecutionType::Bench_X);
  config_proto.mutable_benchx_partitioning()->set_warehouses(total_warehouses);
  for (int i = 0; i < num_regs; i++) {
    auto region = config_proto.add_regions();
    for (int j = 0; j < num_parts; j++) {
      region->add_addresses("localhost");
    }
  }
  config = std::make_shared<slog::Configuration>(config_proto, "localhost");

  string conn_str = "postgresql://root@" + ip + ":" + to_string(CRDB_PORT) + "/geo_bench?sslmode=disable";
  LOG(INFO) << "Connecting to: " << conn_str;

  atomic<bool> running{true};
  vector<thread> workers;
  vector<Stats> all_stats(threads);

  auto start_time = chrono::high_resolution_clock::now();

  for (int i = 0; i < threads; ++i) {
    workers.emplace_back(worker, i, conn_str, params, num_regs, client_reg, ref(running), ref(all_stats[i]), seed);
  }

  LOG(INFO) << "Running for " << duration << " seconds...";
  this_thread::sleep_for(chrono::seconds(duration));
  running = false;

  for (auto& w : workers) w.join();

  auto end_time = chrono::high_resolution_clock::now();
  double total_elapsed_ns = chrono::duration_cast<chrono::nanoseconds>(end_time - start_time).count();

  long committed = 0, aborted = 0, restarts = 0, mp = 0, sp = 0, mh = 0;
  for (auto& s : all_stats) {
    committed += s.committed;
    aborted += s.aborted;
    restarts += s.total_restarts;
    mh += s.multi_home;
    mp += s.multi_partition;
    sp += s.single_partition;
  }

  ofstream summary("summary.csv");
  summary << "committed,aborted,not_started,restarted,single_home,foreign_single_home,multi_home,single_partition,"
             "multi_partition,remaster,elapsed_time\n";
  summary << committed << "," << aborted << ",0," << restarts << "," << (committed - mh) << ",0," << mh << "," << sp
          << "," << mp << ",0," << total_elapsed_ns << "\n";
  summary.close();

  ofstream txns("transactions.csv");
  txns << "txn_id,coordinator,regions,partitions,generator,restarts,global_log_pos,sent_at,received_at,code\n";
  long txn_id = 1;
  for (auto& s : all_stats) {
    for (auto& t : s.transactions) {
      // Logic for regions and partitions string to mimic LSH, FSH, MH
      std::string regions = "0";
      std::string partitions = "0";
      if (t.is_multi_home) {
        regions = "0;1";
        partitions = "0;1";
      } else if (t.is_multi_partition) {
        regions = "0;1"; // For Detock, FSH is simulated when regions differ but partitions is same
        partitions = "0";
      }
      txns << txn_id++ << ",-1," << regions << "," << partitions << ",0," << t.restarts << ",0," << t.sent_at << "," << t.received_at << "," << t.code << "\n";
    }
  }
  txns.close();

  LOG(INFO) << "--- RESULTS ---";
  LOG(INFO) << "Total Committed: " << committed;
  LOG(INFO) << "Total Aborted: " << aborted;
  LOG(INFO) << "Avg. TPS: " << ((double)committed / duration);

  return 0;
}

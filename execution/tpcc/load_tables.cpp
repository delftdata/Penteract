#include "execution/tpcc/load_tables.h"

#include <algorithm>
#include <random>
#include <thread>

#include "common/string_utils.h"
#include "execution/tpcc/table.h"

namespace slog {
namespace tpcc {

namespace {
const size_t kRandStrPoolSize = 1000000;
const std::string kCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz ");
}  // namespace

class PartitionedTPCCDataLoader {
 public:
  PartitionedTPCCDataLoader(const StorageAdapterPtr& storage_adapter, int from_w, int to_w, int step, int seed)
      : rg_(seed), str_gen_(seed), storage_adapter_(storage_adapter), from_w_(from_w), to_w_(to_w), step_(step) {}

  void Load() {

    // Keep track of table size in memory
    size_t total_warehouse_size = 0;
    size_t total_stock_size = 0;
    size_t total_district_size = 0;
    size_t total_customer_size = 0;
    size_t total_history_size = 0;
    size_t total_order_size = 0;
    size_t total_new_order_size = 0;
    size_t total_order_line_size = 0;

    LoadWarehouse(total_warehouse_size, total_stock_size, total_district_size);
    LoadCustomer(total_customer_size, total_history_size);
    LoadOrder(total_order_size, total_new_order_size, total_order_line_size);

    LOG(INFO) << "Total table sizes:";
    LOG(INFO) << "Total Warehouse table size: " << total_warehouse_size;
    LOG(INFO) << "Total Stock table size: " << total_stock_size;
    LOG(INFO) << "Total District table size: " << total_district_size;
    LOG(INFO) << "Total Customer table size: " << total_customer_size;
    LOG(INFO) << "Total History table size: " << total_history_size;
    LOG(INFO) << "Total Order table size: " << total_order_size;
    LOG(INFO) << "Total New Order table size: " << total_new_order_size;
    LOG(INFO) << "Total Order Line table size: " << total_order_line_size;
  }

  static void LoadItem(const StorageAdapterPtr& storage_adapter, const std::vector<int>& rep_w) {
    Table<ItemSchema> item(storage_adapter);
    LOG(INFO) << "Loading " << kMaxItems << " items for each of " << rep_w.size() << " representative warehouses";

    std::mt19937 rg;
    RandomStringGenerator str_rnd;
    std::uniform_int_distribution<> price_rnd(100, 10000);
    // Keep track of table size in memory
    size_t total_item_size = 0;

    for (int id = 1; id <= kMaxItems; id++) {
      auto name = str_rnd(24);
      auto price = price_rnd(rg);
      auto data = str_rnd(50);
      for (auto w_id : rep_w) {
        item.Insert({
            MakeInt32Scalar(w_id),
            MakeInt32Scalar(id),
            MakeInt32Scalar(id),
            MakeFixedTextScalar<24>(name),
            MakeInt32Scalar(price),
            MakeFixedTextScalar<50>(data),
        });

        // Create a string representation for the new row
        std::string row_string;
        row_string.append(std::to_string(w_id)).append(";")
                  .append(std::to_string(id)).append(";")
                  .append(std::to_string(id)).append(";")
                  .append(name).append(";")
                  .append(std::to_string(price)).append(";")
                  .append(data).append(";");
        // Analytically calculate the size of txn_string for the memory footprint
        int cur_row_char_bytes = row_string.capacity();   // allocated chars (includes unused)
        int string_overhead = sizeof(std::string);        // object overhead (on stack)
        int cur_row_total_bytes = cur_row_char_bytes + string_overhead;
        
        total_item_size += cur_row_total_bytes;
      }
    }
    LOG(INFO) << "Total Item table size: " << total_item_size;
  }

 private:
  std::mt19937 rg_;
  RandomStringGenerator str_gen_;

  StorageAdapterPtr storage_adapter_;
  int from_w_;
  int to_w_;
  int step_;

  void LoadStock(int w_id, size_t &total_stock_size) {
    Table<StockSchema> stock(storage_adapter_);

    std::uniform_int_distribution<> quantity_rng(10, 100);

    for (int id = 1; id <= kMaxItems; id++) {
      auto random_quantity = quantity_rng(rg_);
      auto random_long_string = str_gen_(240);
      auto random_short_string = str_gen_(50);
      stock.Insert({
          MakeInt32Scalar(w_id),
          MakeInt32Scalar(id),
          MakeInt16Scalar(random_quantity),
          MakeFixedTextScalar<240>(random_long_string),
          MakeInt32Scalar(0),
          MakeInt16Scalar(0),
          MakeInt16Scalar(0),
          MakeFixedTextScalar<50>(random_short_string),
      });
      // Create a string representation for the new row
      std::string row_string;
      row_string.append(std::to_string(w_id)).append(";")
                .append(std::to_string(id)).append(";")
                .append(std::to_string(random_quantity)).append(";")
                .append(random_long_string).append(";")
                .append(std::to_string(0)).append(";")
                .append(std::to_string(0)).append(";")
                .append(std::to_string(0)).append(";")
                .append(random_short_string).append(";");
      // Analytically calculate the size of txn_string for the memory footprint
      int cur_row_char_bytes = row_string.capacity();   // allocated chars (includes unused)
      int string_overhead = sizeof(std::string);        // object overhead (on stack)
      int cur_row_total_bytes = cur_row_char_bytes + string_overhead;
      
      total_stock_size += cur_row_total_bytes;
    }
  }

  void LoadDistrict(int w_id, size_t &total_district_size) {
    Table<DistrictSchema> district(storage_adapter_);

    std::uniform_int_distribution<> tax_rnd(10, 20);

    for (int id = 1; id <= kDistPerWare; id++) {
      auto random_short_string = str_gen_(10);
      auto random_long_string = str_gen_(71);
      auto random_tax = tax_rnd(rg_);
      // clang-format off
      district.Insert({MakeInt32Scalar(w_id),
                      MakeInt8Scalar(id),
                      MakeFixedTextScalar<10>(random_short_string),
                      MakeFixedTextScalar<71>(random_long_string),
                      MakeInt32Scalar(random_tax),
                      MakeInt64Scalar(30000),
                      MakeInt32Scalar(3001)});
      // clang-format on
      // Create a string representation for the new row
      std::string row_string;
      row_string.append(std::to_string(w_id)).append(";")
                .append(std::to_string(id)).append(";")
                .append(random_short_string).append(";")
                .append(random_long_string).append(";")
                .append(std::to_string(random_tax)).append(";")
                .append(std::to_string(30000)).append(";")
                .append(std::to_string(3001)).append(";");
      // Analytically calculate the size of txn_string for the memory footprint
      int cur_row_char_bytes = row_string.capacity();   // allocated chars (includes unused)
      int string_overhead = sizeof(std::string);        // object overhead (on stack)
      int cur_row_total_bytes = cur_row_char_bytes + string_overhead;
      
      total_district_size += cur_row_total_bytes;
    }
  }

  void LoadWarehouse(size_t &total_warehouse_size, size_t &total_stock_size, size_t &total_district_size) {
    Table<WarehouseSchema> warehouse(storage_adapter_);

    std::uniform_int_distribution<> tax_rnd(10, 20);

    for (int id = from_w_; id < to_w_; id += step_) {
      if (id % 100 == 0) {
        LOG(INFO) << "Loading data for warehouse: " << id << std::endl;
      }
      auto random_short_string = str_gen_(10);
      auto random_long_string = str_gen_(71);
      auto random_tax = tax_rnd(rg_);
      // clang-format off
      warehouse.Insert({MakeInt32Scalar(id),
                        MakeFixedTextScalar<10>(random_short_string),
                        MakeFixedTextScalar<71>(random_long_string),
                        MakeInt32Scalar(random_tax),
                        MakeInt64Scalar(300000)});
      // clang-format on
      // Create a string representation for the new row
      std::string row_string;
      row_string.append(std::to_string(id)).append(";")
                .append(random_short_string).append(";")
                .append(random_long_string).append(";")
                .append(std::to_string(random_tax)).append(";")
                .append(std::to_string(300000)).append(";");
      // Analytically calculate the size of txn_string for the memory footprint
      int cur_row_char_bytes = row_string.capacity();   // allocated chars (includes unused)
      int string_overhead = sizeof(std::string);        // object overhead (on stack)
      int cur_row_total_bytes = cur_row_char_bytes + string_overhead;
      
      total_warehouse_size += cur_row_total_bytes;
      
      LoadStock(id, total_stock_size);
      LoadDistrict(id, total_district_size);
    }
  }

  void LoadCustomer(size_t &total_customer_size, size_t &total_history_size) {
    Table<CustomerSchema> customer(storage_adapter_);
    Table<HistorySchema> history(storage_adapter_);

    std::uniform_int_distribution<> discount_rnd(0, 50);
    std::bernoulli_distribution credit_rnd;

    for (int w_id = from_w_; w_id < to_w_; w_id += step_) {
      if (w_id % 100 == 0) {
        LOG(INFO) << "Loading customers in warehouse: " << w_id << std::endl;
      }
      for (int d_id = 1; d_id <= kDistPerWare; d_id++) {
        for (int id = 1; id <= kCustPerDist; id++) {
          auto string_34 = str_gen_(34);
          auto string_71 = str_gen_(71);
          auto string_16 = str_gen_(16);
          auto random_credit = credit_rnd(rg_) ? "GC" : "BC";
          auto random_discount = discount_rnd(rg_);
          auto string_250 = str_gen_(250);
          customer.Insert({MakeInt32Scalar(w_id), MakeInt8Scalar(d_id), MakeInt32Scalar(id),
                           MakeFixedTextScalar<34>(string_34), MakeFixedTextScalar<71>(string_71),
                           MakeFixedTextScalar<16>(string_16), MakeInt64Scalar(1234567890),
                           MakeFixedTextScalar<2>(random_credit), MakeInt64Scalar(50000),
                           MakeInt32Scalar(random_discount), MakeInt64Scalar(-10), MakeInt64Scalar(10),
                           MakeInt16Scalar(1), MakeInt16Scalar(0), MakeFixedTextScalar<250>(string_250)});
          // Create a string representation for the new row
          std::string row_string;
          row_string.append(std::to_string(w_id)).append(";")
                    .append(std::to_string(d_id)).append(";")
                    .append(std::to_string(id)).append(";")
                    .append(string_34).append(";")
                    .append(string_71).append(";")
                    .append(string_16).append(";")
                    .append(std::to_string(1234567890)).append(";")
                    .append(std::to_string(random_discount)).append(";")
                    .append(std::to_string(-10)).append(";")
                    .append(std::to_string(10)).append(";")
                    .append(std::to_string(1)).append(";")
                    .append(std::to_string(0)).append(";")
                    .append(string_250).append(";");
          // Analytically calculate the size of txn_string for the memory footprint
          int cur_row_char_bytes = row_string.capacity();   // allocated chars (includes unused)
          int string_overhead = sizeof(std::string);        // object overhead (on stack)
          int cur_row_total_bytes = cur_row_char_bytes + string_overhead;
          
          total_customer_size += cur_row_total_bytes;
          
          auto string_24 = str_gen_(24);
          // clang-format off
          history.Insert({MakeInt32Scalar(w_id),
                          MakeInt8Scalar(d_id),
                          MakeInt32Scalar(id),
                          MakeInt32Scalar(1),
                          MakeInt8Scalar(d_id),
                          MakeInt32Scalar(w_id),
                          MakeInt64Scalar(1234567890),
                          MakeInt32Scalar(10),
                          MakeFixedTextScalar<24>(string_24)});
          // clang-format on
          // Create a string representation for the new row
          row_string = "";
          row_string.append(std::to_string(w_id)).append(";")
                    .append(std::to_string(d_id)).append(";")
                    .append(std::to_string(id)).append(";")
                    .append(std::to_string(1)).append(";")
                    .append(std::to_string(d_id)).append(";")
                    .append(std::to_string(w_id)).append(";")
                    .append(std::to_string(1234567890)).append(";")
                    .append(std::to_string(10)).append(";")
                    .append(string_24).append(";");
          // Analytically calculate the size of txn_string for the memory footprint
          cur_row_char_bytes = row_string.capacity();   // allocated chars (includes unused)
          string_overhead = sizeof(std::string);        // object overhead (on stack)
          cur_row_total_bytes = cur_row_char_bytes + string_overhead;
          
          total_history_size += cur_row_total_bytes;
        }
      }
    }
  }

  void LoadOrder(size_t &total_order_size, size_t &total_new_order_size, size_t &total_order_line_size) {
    Table<OrderSchema> order(storage_adapter_);
    Table<NewOrderSchema> new_order(storage_adapter_);
    Table<OrderLineSchema> order_line(storage_adapter_);
    Table<CustomerSchema> customer(storage_adapter_);

    std::uniform_int_distribution<> carrier_id_rnd(1, 10);
    std::uniform_int_distribution<> item_rnd(1, kMaxItems);

    for (int w_id = from_w_; w_id < to_w_; w_id += step_) {
      if (w_id % 100 == 0)
      {
        LOG(INFO) << "Loading orders in warehouse: " << w_id << std::endl;
      }
      for (int d_id = 1; d_id <= kDistPerWare; d_id++) {
        std::vector<int> cust_id(kCustPerDist);
        std::iota(cust_id.begin(), cust_id.end(), 1);
        std::shuffle(cust_id.begin(), cust_id.end(), rg_);
        std::unordered_map<int, int16_t> delivery_count;
        for (int id = 1; id <= kOrdPerDist; id++) {
          auto random_carrier_id = carrier_id_rnd(rg_);
          bool is_delivered = id <= 2100;
          // clang-format off
          order.Insert({MakeInt32Scalar(w_id),
                        MakeInt8Scalar(d_id),
                        MakeInt32Scalar(id),
                        MakeInt32Scalar(cust_id[id - 1]),
                        MakeInt64Scalar(1234567890),
                        MakeInt8Scalar(is_delivered ? random_carrier_id : 0),
                        MakeInt8Scalar(kLinePerOrder),
                        MakeInt8Scalar(1)});
          // clang-format on
          // Create a string representation for the new row
          std::string row_string;
          row_string.append(std::to_string(w_id)).append(";")
                    .append(std::to_string(d_id)).append(";")
                    .append(std::to_string(id)).append(";")
                    .append(std::to_string(cust_id[id - 1])).append(";")
                    .append(std::to_string(1234567890)).append(";")
                    .append(std::to_string(is_delivered ? random_carrier_id : 0)).append(";")
                    .append(std::to_string(kLinePerOrder)).append(";")
                    .append(std::to_string(1)).append(";");
          // Analytically calculate the size of txn_string for the memory footprint
          int cur_row_char_bytes = row_string.capacity();   // allocated chars (includes unused)
          int string_overhead = sizeof(std::string);        // object overhead (on stack)
          int cur_row_total_bytes = cur_row_char_bytes + string_overhead;
          
          total_order_size += cur_row_total_bytes;

          if (is_delivered) {
            delivery_count[cust_id[id - 1]]++;
          }

          if (!is_delivered) {
            new_order.Insert({MakeInt32Scalar(w_id),
                              MakeInt8Scalar(d_id),
                              MakeInt32Scalar(id),
                              MakeInt8Scalar(0)});
            // Create a string representation for the new row
            row_string = "";
            row_string.append(std::to_string(w_id)).append(";")
                      .append(std::to_string(d_id)).append(";")
                      .append(std::to_string(id)).append(";")
                      .append(std::to_string(0)).append(";");
            // Analytically calculate the size of txn_string for the memory footprint
            int cur_row_char_bytes = row_string.capacity();   // allocated chars (includes unused)
            int string_overhead = sizeof(std::string);        // object overhead (on stack)
            int cur_row_total_bytes = cur_row_char_bytes + string_overhead;
            
            total_new_order_size += cur_row_total_bytes;
          }

          for (int ol_id = 1; ol_id <= kLinePerOrder; ol_id++) {
            auto random_item = item_rnd(rg_);
            auto string_24 = str_gen_(24);
            // clang-format off
            order_line.Insert({MakeInt32Scalar(w_id),
                              MakeInt8Scalar(d_id),
                              MakeInt32Scalar(id),
                              MakeInt8Scalar(ol_id),
                              MakeInt32Scalar(random_item),
                              MakeInt32Scalar(w_id),
                              MakeInt64Scalar(is_delivered ? 1234567890 : 0),
                              MakeInt8Scalar(5),
                              MakeInt32Scalar(0),
                              MakeFixedTextScalar<24>(string_24)});
            // clang-format on
            // Create a string representation for the new row
            row_string = "";
            row_string.append(std::to_string(w_id)).append(";")
                      .append(std::to_string(d_id)).append(";")
                      .append(std::to_string(id)).append(";")
                      .append(std::to_string(ol_id)).append(";")
                      .append(std::to_string(random_item)).append(";")
                      .append(std::to_string(w_id)).append(";")
                      .append(std::to_string(is_delivered ? 1234567890 : 0)).append(";")
                      .append(std::to_string(5)).append(";")
                      .append(std::to_string(0)).append(";")
                      .append(string_24).append(";");
            // Analytically calculate the size of txn_string for the memory footprint
            int cur_row_char_bytes = row_string.capacity();   // allocated chars (includes unused)
            int string_overhead = sizeof(std::string);        // object overhead (on stack)
            int cur_row_total_bytes = cur_row_char_bytes + string_overhead;

            total_order_line_size += cur_row_total_bytes;

          }
        }

        for (auto& [c_id, cnt] : delivery_count) {
          customer.Update({MakeInt32Scalar(w_id), MakeInt8Scalar(d_id), MakeInt32Scalar(c_id)},
                          {CustomerSchema::Column::DELIVERY_CNT},
                          {MakeInt16Scalar(cnt)});
        }
      }
    }
  }
};

void LoadTables(const StorageAdapterPtr& storage_adapter, int W, int num_regions, int num_partitions, int partition, int num_threads) {
  LOG(INFO) << "Generating ~" << W / num_partitions << " warehouses using " << num_threads << " threads. ";

  std::vector<int> rep_w;
  int rep_w_counter = partition;
  for (int i = 0; i < num_regions; i++) {
    rep_w.push_back(rep_w_counter + 1);
    rep_w_counter += num_partitions;
  }
  PartitionedTPCCDataLoader::LoadItem(storage_adapter, rep_w);

  std::atomic<int> num_done = 0;
  auto LoadFn = [&](int from_w, int to_w, int seed) {
    PartitionedTPCCDataLoader loader(storage_adapter, from_w, to_w, num_partitions, seed);
    loader.Load();
    num_done++;
  };
  std::vector<std::thread> threads;
  int range = W / num_threads + 1;
  for (int i = 0; i < num_threads; i++) {
    int range_start = i * range;
    int partition_of_range_start = range_start % num_partitions;
    int distance_to_next_in_partition_key = (partition - partition_of_range_start + num_partitions) % num_partitions;
    int from_key = range_start + distance_to_next_in_partition_key;
    int to_key = std::min((i + 1) * range, W);
    threads.emplace_back(LoadFn, from_key + 1, to_key + 1, i);
  }
  while (num_done < num_threads) {
    std::this_thread::sleep_for(std::chrono::seconds(5));
  }
  for (auto& t : threads) {
    t.join();
  }
}

}  // namespace tpcc
}  // namespace slog

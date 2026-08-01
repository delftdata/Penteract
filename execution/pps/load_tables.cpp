#include "execution/pps/load_tables.h"

#include <algorithm>
#include <random>
#include <thread>

#include "common/string_utils.h"
#include "execution/pps/table.h"

namespace slog {
namespace pps {

class PartitionedPPSDataLoader {
 public:
 PartitionedPPSDataLoader(const StorageAdapterPtr& storage_adapter, int num_products, int num_parts, int num_suppliers, 
  int num_regions, int num_partitions, int local_partition, int max_regions, int max_partitions, int num_threads, int seed)
      : num_products_(num_products),
        num_parts_(num_parts),
        num_suppliers_(num_suppliers),
        num_partitions_(num_partitions),
        num_regions_(num_regions),
        local_partition_(local_partition),
        max_regions_(max_regions),
        max_partitions_(max_partitions),
        num_threads_(num_threads),
        seed_(seed),
        rg_(seed),
        storage_adapter_(storage_adapter),
        num_parts_per_class_(num_parts / (num_partitions * num_regions)) {
          remote_regions_ = std::vector<std::vector<int>>(num_regions_);
          for (int i = 0; i < num_regions_; i++) {
            remote_regions_[i].reserve(num_regions_ - 1);
            for (int j = 0; j < num_regions_; j++) {
              if (i != j) {
                remote_regions_[i].push_back(j);
              }
            }
          }
          remote_partitions_.reserve(num_partitions_ - 1);
          for (int i = 0; i < num_partitions_; i++) {
            if (i != local_partition_) {
              remote_partitions_.push_back(i);
            }
          }
        }

  void Load(int thread_index) {
    LOG(INFO) << showThreadIndex(thread_index) << " Loading PPS data with seed " << seed_;
    const size_t frequencyLog = 5; // Number of times to log per table 

    // Keep track of table size in memory
    size_t total_products_size = 0;
    size_t total_parts_size = 0;
    size_t total_suppliers_size = 0;
    size_t total_product_parts_size = 0;
    size_t total_supplier_parts_size = 0;

    // Add the table for the products
    size_t productTableApproximateSize = num_products_ / num_partitions_ / num_threads_;
    size_t productTableLogCycle = std::max(size_t{1}, productTableApproximateSize / frequencyLog);
    LOG(INFO) << showThreadIndex(thread_index) << " Generating ~" << productTableApproximateSize << " products";
    Table<ProductSchema> productTable(storage_adapter_); 
    size_t productTableRecordsCount = 0, productCountWithThread = 0;
    for (uint32_t product_id = 1; product_id <= num_products_; product_id++) {
      if (computePartition(product_id) == local_partition_) {
        if (product_id % 1000000 == 0) {
          LOG(INFO) << "Load the product with id " << product_id;
        }
        auto random_string = str_gen_(10);
        productTable.Insert({
            MakeInt32Scalar(product_id),
            MakeFixedTextScalar<10>(random_string)
        });
        // Create a string representation for the new row
        std::string row_string;
        row_string.append(std::to_string(product_id)).append(";")
                  .append(random_string).append(";");
        // Analytically calculate the size of txn_string for the memory footprint
        int cur_row_char_bytes = row_string.capacity();   // allocated chars (includes unused)
        int string_overhead = sizeof(std::string);        // object overhead (on stack)
        int cur_row_total_bytes = cur_row_char_bytes + string_overhead;
        
        total_products_size += cur_row_total_bytes;
      }
      productCountWithThread++;
      if (productCountWithThread % num_threads_ != thread_index) {
        continue;
      }
      productTableRecordsCount++;
      if (productTableRecordsCount % productTableLogCycle == 0) {
        LOG(INFO) << showThreadIndex(thread_index) << " Load the " << productTableRecordsCount 
          << "th record in the Products Table with product id " << product_id;
      }
      auto random_string = str_gen_(10);
      productTable.Insert({
          MakeInt32Scalar(product_id),
          MakeFixedTextScalar<10>(random_string)
      });
      // Create a string representation for the new row
      std::string row_string;
      row_string.append(std::to_string(product_id)).append(";")
                .append(random_string).append(";");
      // Analytically calculate the size of txn_string for the memory footprint
      int cur_row_char_bytes = row_string.capacity();   // allocated chars (includes unused)
      int string_overhead = sizeof(std::string);        // object overhead (on stack)
      int cur_row_total_bytes = cur_row_char_bytes + string_overhead;
      total_products_size += cur_row_total_bytes;
    }
    LOG(INFO) << showThreadIndex(thread_index) << " Loaded " << productTableRecordsCount << " records in the Products Table";

    // Add the table for the parts
    size_t partTableApproximateSize = num_parts_ / num_partitions_ / num_threads_;
    size_t partTableLogCycle = std::max(size_t{1}, partTableApproximateSize / frequencyLog);
    LOG(INFO) << showThreadIndex(thread_index) << " Generating ~" << partTableApproximateSize << " parts";
    Table<PartSchema> partTable(storage_adapter_);
    size_t partTableRecordsCount = 0, partCountWithThread = 0;
    for (uint32_t part_id = 1; part_id <= num_parts_; part_id++) {
      if (computePartition(part_id) != local_partition_) {
        continue;
      }
      partCountWithThread++;
      if (partCountWithThread % num_threads_ != thread_index) {
        continue;
      }
      partTableRecordsCount++;
      if (partTableRecordsCount % partTableLogCycle == 0) {
        LOG(INFO) << showThreadIndex(thread_index) << " Load the " << partTableRecordsCount 
          << "th record in the Parts Table with part id " << part_id;
      }
      partTable.Insert({
          MakeInt32Scalar(part_id),
          MakeInt64Scalar(1000 + (part_id % 100)),
      });
      // Create a string representation for the new row
      std::string row_string;
      row_string.append(std::to_string(part_id)).append(";")
                .append(std::to_string(1000 + (part_id % 100))).append(";");
      // Analytically calculate the size of txn_string for the memory footprint
      int cur_row_char_bytes = row_string.capacity();   // allocated chars (includes unused)
      int string_overhead = sizeof(std::string);        // object overhead (on stack)
      int cur_row_total_bytes = cur_row_char_bytes + string_overhead;
      total_parts_size += cur_row_total_bytes;
    }
    LOG(INFO) << showThreadIndex(thread_index) << " Loaded " << partTableRecordsCount << " records in the Parts Table";

    // Add the table for the suppliers
    size_t supplierTableApproximateSize = num_suppliers_ / num_partitions_ / num_threads_;
    size_t supplierTableLogCycle = std::max(size_t{1}, supplierTableApproximateSize / frequencyLog);
    LOG(INFO) << showThreadIndex(thread_index) << " Generating ~" << supplierTableApproximateSize << " suppliers";
    Table<SupplierSchema> supplierTable(storage_adapter_);
    size_t supplierTableRecordsCount = 0, supplierCountWithThread = 0;
    for (uint32_t supplier_id = 1; supplier_id <= num_suppliers_; supplier_id++) {
      if (computePartition(supplier_id) != local_partition_) {
        continue;
      }
      supplierCountWithThread++;
      if (supplierCountWithThread % num_threads_ != thread_index) {
        continue;
      }
      supplierTableRecordsCount++;
      if (supplierTableRecordsCount % supplierTableLogCycle == 0) {
        LOG(INFO) << showThreadIndex(thread_index) << " Load the " << supplierTableRecordsCount 
          << "th record in the Suppliers Table with supplier id " << supplier_id;
      }
      auto random_string = str_gen_(10);
      supplierTable.Insert({
          MakeInt32Scalar(supplier_id),
          MakeFixedTextScalar<10>(random_string)
      });
      // Create a string representation for the new row
      std::string row_string;
      row_string.append(std::to_string(supplier_id)).append(";")
                .append(random_string).append(";");
      // Analytically calculate the size of txn_string for the memory footprint
      int cur_row_char_bytes = row_string.capacity();   // allocated chars (includes unused)
      int string_overhead = sizeof(std::string);        // object overhead (on stack)
      int cur_row_total_bytes = cur_row_char_bytes + string_overhead;
      total_suppliers_size += cur_row_total_bytes;
    }
    LOG(INFO) << showThreadIndex(thread_index) << " Loaded " << supplierTableRecordsCount << " records in the Suppliers Table";

    // Add the product-to-parts mappings table
    size_t productPartsTableApproximateSize = num_products_ * pps::kPartsPerProduct / num_partitions_ / num_threads_;
    size_t productPartsTableLogCycle = std::max(size_t{1}, productPartsTableApproximateSize / frequencyLog / pps::kPartsPerProduct);
    LOG(INFO) << showThreadIndex(thread_index) << " Generating ~" << productPartsTableApproximateSize << " product-parts mappings";
    CHECK(num_parts_per_class_ >= 4) << "Not enough parts per class for each category: " << num_parts_per_class_;
    Table<ProductPartsSchema> productPartsTable(storage_adapter_);
    size_t productPartsTableRecordsCount = 0, productPartsCountWithThread = 0;
    for (uint32_t product_id = 1; product_id <= num_products_; product_id++) { 
      if (computePartition(product_id) != local_partition_) {
        continue;
      }
      productPartsCountWithThread++;
      if (productPartsCountWithThread % num_threads_ != thread_index) {
        continue;
      }     
      int product_region = computeRegion(product_id);
      std::vector<int> selected_parts;
      int category = (productPartsTableRecordsCount / num_regions_) % 4;
      switch (category) {
        case 0:
          // Category 1: same region, same partition
          for (int i = 1; i <= pps::kPartsPerProduct; i++) {
            int part_id = chooseRandomPart(product_region, local_partition_);
            CHECK(part_id > 0 && part_id <= num_parts_) << "Invalid part id: " << part_id;
            selected_parts.push_back(part_id);
            productPartsTable.Insert({
              MakeInt32Scalar(product_id),
              MakeInt32Scalar(i),
              MakeInt32Scalar(part_id)
            });
            // Create a string representation for the new row
            std::string row_string;
            row_string.append(std::to_string(product_id)).append(";")
                      .append(std::to_string(i)).append(";")
                      .append(std::to_string(part_id)).append(";");
            // Analytically calculate the size of txn_string for the memory footprint
            int cur_row_char_bytes = row_string.capacity();   // allocated chars (includes unused)
            int string_overhead = sizeof(std::string);        // object overhead (on stack)
            int cur_row_total_bytes = cur_row_char_bytes + string_overhead;
            
            total_product_parts_size += cur_row_total_bytes;
          }
          break;
          
        case 1:
          // Category 2: same region, different partitions
          std::shuffle(remote_partitions_.begin(), remote_partitions_.end(), rg_);
          for (int i = 1; i <= pps::kPartsPerProduct; i++) {
            int chosen_partition_index = std::uniform_int_distribution<int>(0, max_partitions_ - 1)(rg_);
            int chosen_partition = (chosen_partition_index == max_partitions_ - 1 ?
              local_partition_ : remote_partitions_[chosen_partition_index]);
            int part_id = chooseRandomPart(product_region, chosen_partition);
            CHECK(part_id > 0 && part_id <= num_parts_) << "Invalid part id: " << part_id;
            selected_parts.push_back(part_id);
            productPartsTable.Insert({
              MakeInt32Scalar(product_id),
              MakeInt32Scalar(i),
              MakeInt32Scalar(part_id)
            });
            // Create a string representation for the new row
            std::string row_string;
            row_string.append(std::to_string(product_id)).append(";")
                      .append(std::to_string(i)).append(";")
                      .append(std::to_string(part_id)).append(";");
            // Analytically calculate the size of txn_string for the memory footprint
            int cur_row_char_bytes = row_string.capacity();   // allocated chars (includes unused)
            int string_overhead = sizeof(std::string);        // object overhead (on stack)
            int cur_row_total_bytes = cur_row_char_bytes + string_overhead;
            
            total_product_parts_size += cur_row_total_bytes;
          }
          break;

        case 2:
          // Category 3: different regions, same partition
          std::shuffle(remote_regions_[product_region].begin(), remote_regions_[product_region].end(), rg_);
          for (int i = 1; i <= pps::kPartsPerProduct; i++) {
            int chosen_region_index = std::uniform_int_distribution<int>(0, max_regions_ - 1)(rg_);
            int chosen_region = (chosen_region_index == max_regions_ - 1 ?
              product_region : remote_regions_[product_region][chosen_region_index]);
            int part_id = chooseRandomPart(chosen_region, local_partition_);
            CHECK(part_id > 0 && part_id <= num_parts_) << "Invalid part id: " << part_id;
            selected_parts.push_back(part_id);
            productPartsTable.Insert({
              MakeInt32Scalar(product_id),
              MakeInt32Scalar(i),
              MakeInt32Scalar(part_id)
            });
            // Create a string representation for the new row
            std::string row_string;
            row_string.append(std::to_string(product_id)).append(";")
                      .append(std::to_string(i)).append(";")
                      .append(std::to_string(part_id)).append(";");
            // Analytically calculate the size of txn_string for the memory footprint
            int cur_row_char_bytes = row_string.capacity();   // allocated chars (includes unused)
            int string_overhead = sizeof(std::string);        // object overhead (on stack)
            int cur_row_total_bytes = cur_row_char_bytes + string_overhead;
            
            total_product_parts_size += cur_row_total_bytes;
          }
          break;

        case 3:
          // Category 4: different regions, different partitions
          std::shuffle(remote_partitions_.begin(), remote_partitions_.end(), rg_);
          std::shuffle(remote_regions_[product_region].begin(), remote_regions_[product_region].end(), rg_);
          for (int i = 1; i <= pps::kPartsPerProduct; i++) {
            int chosen_partition_index = std::uniform_int_distribution<int>(0, max_partitions_ - 1)(rg_);
            int chosen_partition = (chosen_partition_index == max_partitions_ - 1 ?
              local_partition_ : remote_partitions_[chosen_partition_index]);
            int chosen_region_index = std::uniform_int_distribution<int>(0, max_regions_ - 1)(rg_);
            int chosen_region = (chosen_region_index == max_regions_ - 1 ?
              product_region : remote_regions_[product_region][chosen_region_index]);
            int part_id = chooseRandomPart(chosen_region, chosen_partition);
            CHECK(part_id > 0 && part_id <= num_parts_) << "Invalid part id: " << part_id;
            selected_parts.push_back(part_id);
            productPartsTable.Insert({
              MakeInt32Scalar(product_id),
              MakeInt32Scalar(i),
              MakeInt32Scalar(part_id)
            });
            // Create a string representation for the new row
            std::string row_string;
            row_string.append(std::to_string(product_id)).append(";")
                      .append(std::to_string(i)).append(";")
                      .append(std::to_string(part_id)).append(";");
            // Analytically calculate the size of txn_string for the memory footprint
            int cur_row_char_bytes = row_string.capacity();   // allocated chars (includes unused)
            int string_overhead = sizeof(std::string);        // object overhead (on stack)
            int cur_row_total_bytes = cur_row_char_bytes + string_overhead;
            
            total_product_parts_size += cur_row_total_bytes;
          }
          break;
      }
      productPartsTableRecordsCount++;
      if (productPartsTableRecordsCount % productPartsTableLogCycle == 0) {
        LOG(INFO) << showThreadIndex(thread_index) << " Load the " << productPartsTableRecordsCount * pps::kPartsPerProduct 
          << "th record in the ProductParts Table with product " << showIdWithPartitionAndRegion(product_id) 
          << " of category " << category << " and the parts " << showChosenParts(selected_parts);
      }
    }
    LOG(INFO) << showThreadIndex(thread_index) << " Loaded " << productPartsTableRecordsCount * pps::kPartsPerProduct 
      << " records in the ProductParts Table";

    // Add the supplier-to-parts mappings table
    size_t supplierPartsTableApproximateSize = num_suppliers_ * pps::kPartsPerSupplier / num_partitions_ / num_threads_;
    size_t supplierPartsTableLogCycle = std::max(size_t{1}, productPartsTableApproximateSize / frequencyLog / pps::kPartsPerSupplier);
    LOG(INFO) << showThreadIndex(thread_index) << " Generating ~" << supplierPartsTableApproximateSize << " supplier-parts mappings";
    Table<SupplierPartsSchema> supplierPartsTable(storage_adapter_);
    size_t supplierPartsTableRecordsCount = 0, supplierPartsCountWithThread = 0;
    std::uniform_int_distribution<int> part_ids_generator(1, num_parts_);
    for (uint32_t supplier_id = 1; supplier_id <= num_suppliers_; supplier_id++) {
      if (computePartition(supplier_id) != local_partition_) {
        continue;
      }
      supplierPartsCountWithThread++;
      if (supplierPartsCountWithThread % num_threads_ != thread_index) {
        continue;
      }
      std::vector<int> selected_parts;
      for (int i = 1; i <= pps::kPartsPerSupplier; i++) {
        int part_id = part_ids_generator(rg_);
        selected_parts.push_back(part_id);
        supplierPartsTable.Insert({
            MakeInt32Scalar(supplier_id),
            MakeInt32Scalar(i),
            MakeInt32Scalar(part_id)
        });
        // Create a string representation for the new row
        std::string row_string;
        row_string.append(std::to_string(supplier_id)).append(";")
                  .append(std::to_string(i)).append(";")
                  .append(std::to_string(part_id)).append(";");
        // Analytically calculate the size of txn_string for the memory footprint
        int cur_row_char_bytes = row_string.capacity();   // allocated chars (includes unused)
        int string_overhead = sizeof(std::string);        // object overhead (on stack)
        int cur_row_total_bytes = cur_row_char_bytes + string_overhead;
        total_supplier_parts_size += cur_row_total_bytes;
      }
      supplierPartsTableRecordsCount++;
      if (supplierPartsTableRecordsCount % supplierPartsTableLogCycle == 0) {
        LOG(INFO) << showThreadIndex(thread_index) << " Load the " << supplierPartsTableRecordsCount * pps::kPartsPerSupplier 
          << "th record in the SupplierParts Table with supplier " << showIdWithPartitionAndRegion(supplier_id)
          << " and the parts " << showChosenParts(selected_parts);
      }
    }
    LOG(INFO) << showThreadIndex(thread_index) << " Loaded " << supplierPartsTableRecordsCount * pps::kPartsPerSupplier 
      << " records in the SupplierParts Table";

    LOG(INFO) << "=========================";
    LOG(INFO) << showThreadIndex(thread_index) << "Total table sizes";
    LOG(INFO) << "Total Product table size: " << total_products_size;
    LOG(INFO) << "Total Parts table size: " << total_parts_size;
    LOG(INFO) << "Total Supplier table size: " << total_suppliers_size;
    LOG(INFO) << "Total Product Parts table size: " << total_product_parts_size;
    LOG(INFO) << "Total Supplier Parts table size: " << total_supplier_parts_size;
    LOG(INFO) << "=========================";
  }

 private:
  int chooseRandomPart(int chosen_region, int chosen_partition) {
    int normalized_part_id = num_partitions_ * chosen_region + chosen_partition + 1;
    int part_index_within_class = std::uniform_int_distribution<int>(1, num_parts_per_class_)(rg_);
    return (part_index_within_class - 1) * num_partitions_ * num_regions_ + normalized_part_id;
  };

  int computePartition(int id) const {
    return (id - 1) % num_partitions_;
  }

  int computeRegion(int id) const {
    return (id - 1) / num_partitions_ % num_regions_;
  }

  std::string showIdWithPartitionAndRegion(int id) const {
    return std::to_string(id) + "(" + std::to_string(computeRegion(id)) + "," + std::to_string(computePartition(id)) + ")";
  }

  std::string showChosenParts(const std::vector<int>& chosen_parts) const {
    if (chosen_parts.empty()) {
      return "[]";
    }
    std::ostringstream oss;
    oss << "[" << showIdWithPartitionAndRegion(chosen_parts[0]);
    for (size_t i = 1; i < chosen_parts.size(); i++) {
      oss << ", " << showIdWithPartitionAndRegion(chosen_parts[i]);
    }
    oss << "]";
    return oss.str();
  }

  std::string showThreadIndex(int thread_index) const {
    return "[Thread " + std::to_string(thread_index) + "]";
  }

  int num_products_;
  int num_parts_;
  int num_suppliers_;
  int num_partitions_;
  int num_regions_;

  // The table maintained by a database node is dependent only on the partition, not on the region.
  int local_partition_;
  std::vector<std::vector<int>> remote_regions_;
  std::vector<int> remote_partitions_;

  int max_regions_;
  int max_partitions_;

  int num_threads_;

  /*
   * We define as class the combination of a region and a partition. So, the number of classes is num_partitions * num_regions.
   * The number of parts per class is the number of parts divided by the number of classes (we round for simplicity).
   * For example, if we have 3 regions and 4 partitions, the class (0, 2) will contain the parts from
   * the region 0 and the partition 2 => [3, 15, 27, ...]
   * 
   * partition / region |  0  |  1  |  2  |  0  |  1  |  2  |  0  |  1  |  2
   * -------------------|-----|-----|-----|-----|-----|-----|-----|-----|-----
   *           0        |  1  |  5  |  9  | 13  | 17  | 21  | 25  | 29  | 33
   *           1        |  2  |  6  | 10  | 14  | 18  | 22  | 26  | 30  | 34
   *           2        |  3  |  7  | 11  | 15  | 19  | 23  | 27  | 31  | 35
   *           3        |  4  |  8  | 12  | 16  | 20  | 24  | 28  | 32  | 36
   * ---------------------------------- parts ids ----------------------------
   */
  int num_parts_per_class_;

  int seed_;
  std::mt19937 rg_;
  RandomStringGenerator str_gen_;

  StorageAdapterPtr storage_adapter_;
};

void LoadTables(const StorageAdapterPtr& storage_adapter, int num_products, int num_parts, int num_suppliers, 
  int num_regions, int num_partitions, int local_partition, int max_regions, int max_partitions, int num_threads) { 
    LOG(INFO) << "Starting PPS data loading with " << num_threads << " threads";
    auto LoadFn = [&](int thread_index) {
      PartitionedPPSDataLoader data_loader(storage_adapter, num_products, num_parts, num_suppliers, 
        num_regions, num_partitions, local_partition, max_regions, max_partitions, num_threads, 
        num_threads * local_partition + thread_index);
      data_loader.Load(thread_index);
    };
    std::vector<std::thread> dataGenerationThreads;
    for (int i = 0; i < num_threads; i++) {
      dataGenerationThreads.emplace_back(LoadFn, i);
    }
    for (auto& t : dataGenerationThreads) {
      t.join();
    }
    LOG(INFO) << "Total number of accessible records: " << storage_adapter->getKeyCount();
}

}  // namespace pps
}  // namespace slog
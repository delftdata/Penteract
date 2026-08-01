#include "execution/dsh/load_tables.h"

#include <algorithm>
#include <random>
#include <thread>
#include <string>

#include "execution/dsh/table.h"
#include "common/string_utils.h"
#include "execution/dsh/utils.h"

namespace{
    const std::string kCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz ");
}

namespace slog {
namespace dsh {

class PartitionedDSHLoader {
    public:
        PartitionedDSHLoader(const StorageAdapterPtr& storage_adapter, int num_p, int partition, int num_r, int num_u, int num_h, double max_coord, int seed)
        : rg_(seed), str_gen_(seed), storage_adapter_(storage_adapter), num_p_(num_p), partition_(partition), num_r_(num_r), num_u_(num_u), num_h_(num_h), coord_range_(max_coord) {}

        // Load the database tables which need data beforehand
        void Load() {
            LoadUsers();
            LoadHotels();
        }

    private:
        std::mt19937 rg_;
        RandomStringGenerator str_gen_;
    
        StorageAdapterPtr storage_adapter_;
        uint32_t partition_;
        uint32_t num_p_;
        uint32_t num_r_;
        
        uint32_t num_u_;
        uint32_t num_h_;
        double coord_range_;

        void LoadUsers() {
            Table<UserSchema> users(storage_adapter_);
            // Keep track of table size in memory
            size_t total_users_size = 0;

            // This for loop is equivalent to pps but no checking
            for (uint32_t i = partition_; i < num_u_; i += num_p_) {
                users.Insert({
                    MakeFixedTextScalar<20>(format_uname(std::to_string(i))),
                    MakeVarTextScalar<60>(std::to_string(i))
                });
                // Create a string representation for the new row
                std::string row_string;
                row_string.append(format_uname(std::to_string(i))).append(";")
                          .append(std::to_string(i)).append(";");
                // Analytically calculate the size of txn_string for the memory footprint
                int cur_row_char_bytes = row_string.capacity();   // allocated chars (includes unused)
                int string_overhead = sizeof(std::string);        // object overhead (on stack)
                int cur_row_total_bytes = cur_row_char_bytes + string_overhead;
                
                total_users_size += cur_row_total_bytes;

                if (i % 10000000 == 0) {
                    LOG(INFO) << "Load user with ID " << i;
                }
            }
            LOG(INFO) << "Total User table size: " << total_users_size;
        }

        void LoadHotels() {
            Table<HotelSchema> hotels(storage_adapter_);
            // Keep track of table size in memory
            size_t total_hotels_size = 0;

            std::uniform_real_distribution<> coord_rnd(0.0, coord_range_);
            std::uniform_real_distribution<> rating_rnd(0.0, 5.0);
            std::uniform_real_distribution<> price_rnd(0.0, kMaxHotelPrice);
            std::uniform_int_distribution<> capacity_rnd(kMinHotelCapacity, kMaxHotelCapacity);

            for (uint32_t i = partition_; i < num_h_; i += num_p_) {
                auto random_coord_1 = coord_rnd(rg_);
                auto random_coord_2 = coord_rnd(rg_);
                auto random_rating = rating_rnd(rg_);
                auto random_price = price_rnd(rg_);
                auto random_capa = capacity_rnd(rg_);
                hotels.Insert({
                    MakeInt32Scalar(i),
                    MakeFloat64Scalar(random_coord_1),
                    MakeFloat64Scalar(random_coord_2),
                    MakeFloat64Scalar(random_rating),
                    MakeFloat64Scalar(random_price),
                    MakeInt32Scalar(random_capa),
                });
                // Create a string representation for the new row
                std::string row_string;
                row_string.append(std::to_string(i)).append(";")
                          .append(std::to_string(random_coord_1)).append(";")
                          .append(std::to_string(random_coord_2)).append(";")
                          .append(std::to_string(random_rating)).append(";")
                          .append(std::to_string(random_price)).append(";")
                          .append(std::to_string(random_capa)).append(";");
                // Analytically calculate the size of txn_string for the memory footprint
                int cur_row_char_bytes = row_string.capacity();   // allocated chars (includes unused)
                int string_overhead = sizeof(std::string);        // object overhead (on stack)
                int cur_row_total_bytes = cur_row_char_bytes + string_overhead;
                
                total_hotels_size += cur_row_total_bytes;

                if (i % 10000000 == 0) {
                    LOG(INFO) << "Load hotel with ID " << i; 
                }
            }
            LOG(INFO) << "Total Hotel table size: " << total_hotels_size;
        }

        int computePartition(int id) const {
            return id % num_p_;
        }

        int computeRegion(int id) const {
            return id / num_p_ % num_r_;
        }
};


void LoadTables(const StorageAdapterPtr& storage_adapter, int num_partitions, int partition, int num_regions, int num_users, int num_hotels,
    double coord_range, int num_threads) {
    // Populate users
    PartitionedDSHLoader loader(storage_adapter, num_partitions, partition, num_regions, num_users, num_hotels, coord_range, time(nullptr));
    loader.Load();
}

} // namespace dsh
} // namespace slog

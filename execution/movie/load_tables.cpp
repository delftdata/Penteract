#include "execution/movie/load_tables.h"
#include <string>
#include <array>

#include "common/string_utils.h"
#include "execution/movie/table.h"
#include "execution/movie/constants.h"


namespace slog{
namespace movie{

class PartitionedMovieDataLoader {
    public:
        PartitionedMovieDataLoader(const StorageAdapterPtr& storage_adapter, int num_users)
        : storage_adapter_(storage_adapter), num_users_(num_users) {}
        void Load() {
            LoadMovie();
            LoadUser(num_users_);
        }
        
    private:
        StorageAdapterPtr storage_adapter_;
        int num_users_;
        
        void LoadUser(int num_users) {
            Table<UserSchema> user(storage_adapter_);
            // Keep track of table size in memory
            size_t total_user_size = 0;

            LOG(INFO) << "Loading " << num_users_ << " users ....";
            for (int i = 1; i <= num_users_; i++) {
                //LOG(INFO) << "Loading user " << i;

                std::string usernameprefix = std::to_string(i);
                addLeadingZeros(12, usernameprefix);

                std::string postfix = std::to_string(i);
                addLeadingZeros(12, postfix);

                std::string first_name = "first_name_" + postfix;
                std::string last_name = "last_name_" + postfix;
                std::string username = usernameprefix + "_username";
                std::string password = "password_" + postfix;
                int user_id = i;
                user.Insert({
                    MakeFixedTextScalar<21>(username),
                    MakeInt64Scalar(user_id),
                    MakeFixedTextScalar<21>(password),
                    MakeFixedTextScalar<22>(last_name),
                    MakeFixedTextScalar<23>(first_name)
                });

                // Create a string representation for the new row
                std::string row_string;
                row_string.append(username).append(";")
                          .append(std::to_string(user_id)).append(";")
                          .append(password).append(";")
                          .append(last_name).append(";")
                          .append(first_name).append(";");
                // Analytically calculate the size of txn_string for the memory footprint
                int cur_row_char_bytes = row_string.capacity();   // allocated chars (includes unused)
                int string_overhead = sizeof(std::string);        // object overhead (on stack)
                int cur_row_total_bytes = cur_row_char_bytes + string_overhead;
                
                total_user_size += cur_row_total_bytes;

                if (i % 10000000 == 0) {
                    LOG(INFO) << "Generated " << i << " records, with total size " << total_user_size;
                }
            }
            LOG(INFO) << "Total User table size: " << total_user_size;
        }

        void LoadMovie() {
            Table<MovieSchema> movie(storage_adapter_);
            // Keep track of table size in memory
            size_t total_movie_size = 0;

            LOG(INFO) << "Loading " << (sizeof(movies)/sizeof(*movies)) << " movies ....";
            for (int i = 0; i < (sizeof(movies)/sizeof(*movies)); i++) {
                
                /*// To debug poor performance of Janus, try a huge dataset with fictional movie names
                for (int j = 0; j < 1000; j++) {
                    std::string titleprefix = std::to_string(i * 1000 + j + 1);
                    addLeadingZeros(12, titleprefix);
                    std::string movie_id = std::to_string(i * 1000 + j + 1);
                    addLeadingZeros(8, movie_id);
                    std::string title = titleprefix + "_" + movie_id;
                    addTrailingSpaces(100, title);
                    movie.Insert({
                        MakeFixedTextScalar<100>(title),
                        MakeFixedTextScalar<8>(movie_id),
                    });
                }*/

                //LOG(INFO) << "Currrent movie is " << movies[i];
                std::string titleprefix = std::to_string(i + 1);
                addLeadingZeros(12, titleprefix);

                std::string movie_id = std::to_string(i + 1);
                addLeadingZeros(4, movie_id);
                std::string title = titleprefix + "_" + movies[i];
                addTrailingSpaces(100, title);
                movie.Insert({
                    MakeFixedTextScalar<100>(title),
                    MakeFixedTextScalar<4>(movie_id),
                    MakeInt64Scalar(0L),
                    MakeInt64Scalar(0L),
                });

                // Create a string representation for the new row
                std::string row_string;
                row_string.append(title).append(";")
                          .append(movie_id).append(";")
                          .append(std::to_string(0L)).append(";")
                          .append(std::to_string(0L)).append(";");
                // Analytically calculate the size of txn_string for the memory footprint
                int cur_row_char_bytes = row_string.capacity();   // allocated chars (includes unused)
                int string_overhead = sizeof(std::string);        // object overhead (on stack)
                int cur_row_total_bytes = cur_row_char_bytes + string_overhead;
                
                total_movie_size += cur_row_total_bytes;
            }
            LOG(INFO) << "Total Movie table size: " << total_movie_size;
        }
};

void LoadTables(const StorageAdapterPtr& storage_adapter, int num_users, int num_regions, int num_partitions, int partition, int num_threads) {
                    PartitionedMovieDataLoader loader(storage_adapter, num_users);
                    loader.Load();
                }

}  // namespace movie
}  // namespace slog
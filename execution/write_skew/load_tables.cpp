#include "execution/write_skew/load_tables.h"

#include <glog/logging.h>

#include "execution/write_skew/table.h"

namespace slog {
namespace write_skew {

void LoadWriteSkewPairs(const StorageAdapterPtr& storage_adapter, int num_pairs, int num_regions, int num_partitions,
                        int partition) {
  Table<WriteSkewSchema> table(storage_adapter);
  int loaded = 0;
  // Each pair consists of two rows: id = 2*i-1 and id = 2*i for i in [1, num_pairs].
  // Both rows start with VALUE=0.
  for (int i = 1; i <= num_pairs; i++) {
    for (int offset = 0; offset < 2; offset++) {
      int id = 2 * i - 1 + offset;
      // Determine partition using the same formula as WriteSkewSharder:
      // partition = (id - 1) % num_partitions
      if ((id - 1) % num_partitions == partition) {
        table.Insert({MakeInt32Scalar(id), MakeInt32Scalar(0)});
        loaded++;
      }
    }
  }
  LOG(INFO) << "Loaded " << loaded << " write_skew rows for partition " << partition;
}

}  // namespace write_skew
}  // namespace slog

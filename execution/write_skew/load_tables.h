#pragma once

#include "execution/write_skew/storage_adapter.h"

namespace slog {
namespace write_skew {

void LoadWriteSkewPairs(const StorageAdapterPtr& storage_adapter, int num_pairs, int num_regions, int num_partitions,
                        int partition);

}  // namespace write_skew
}  // namespace slog

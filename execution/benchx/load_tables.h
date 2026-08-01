#pragma once

#include "execution/benchx/constants.h"
#include "execution/benchx/storage_adapter.h"

namespace slog {
namespace benchx {

void LoadTables(const StorageAdapterPtr& storage_adapter, int W, int num_regions, int num_partitions, int partition,
                int num_threads = 3);

}  // namespace benchx
}  // namespace slog
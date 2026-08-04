#include "execution/write_skew/transaction.h"

namespace slog {
namespace write_skew {

WriteSkewTxn::WriteSkewTxn(const StorageAdapterPtr& storage_adapter, int own_id, int other_id)
    : table_(storage_adapter), own_id_(own_id), other_id_(other_id) {}

bool WriteSkewTxn::Read() {
  auto own_row = table_.Select({MakeInt32Scalar(own_id_)}, {WriteSkewSchema::Column::VALUE});
  own_val_ = own_row.empty() ? 0 : UncheckedCast<Int32Scalar>(own_row[0])->value;

  auto other_row = table_.Select({MakeInt32Scalar(other_id_)}, {WriteSkewSchema::Column::VALUE});
  other_val_ = other_row.empty() ? 0 : UncheckedCast<Int32Scalar>(other_row[0])->value;

  return true;
}

void WriteSkewTxn::Compute() {
  if (own_val_ + other_val_ < 1) {
    own_val_ = 1;
  }
}

bool WriteSkewTxn::Write() {
  if (own_val_ == 0) return true;
  auto row = table_.Select({MakeInt32Scalar(own_id_)});
  if (row.empty()) {
    return table_.Insert({MakeInt32Scalar(own_id_), MakeInt32Scalar(1)});
  }
  return table_.Update({MakeInt32Scalar(own_id_)}, {WriteSkewSchema::Column::VALUE}, {MakeInt32Scalar(1)});
}

}  // namespace write_skew
}  // namespace slog

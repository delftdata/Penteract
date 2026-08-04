#pragma once

#include "execution/write_skew/table.h"

namespace slog {
namespace write_skew {

class WriteSkewTransaction {
 public:
  virtual ~WriteSkewTransaction() = default;
  bool Execute() {
    if (!Read()) {
      return false;
    }
    Compute();
    if (!Write()) {
      return false;
    }
    return true;
  }
  virtual bool Read() = 0;
  virtual void Compute() = 0;
  virtual bool Write() = 0;

  const std::string& error() const { return error_; }

 protected:
  void SetError(const std::string& error) {
    if (error_.empty()) error_ = error;
  }

 private:
  std::string error_;
};

// Probes for write skew using two separate rows per pair.
// own_id is the row this transaction writes; other_id is the row it reads.
// Txn A: WRITE own_id, READ other_id.
// Txn B: WRITE other_id, READ own_id.
// No write-write conflict -> a non-serializable protocol can exhibit write skew.
class WriteSkewTxn : public WriteSkewTransaction {
 public:
  WriteSkewTxn(const StorageAdapterPtr& storage_adapter, int own_id, int other_id);
  bool Read() final;
  void Compute() final;
  bool Write() final;

 private:
  Table<WriteSkewSchema> table_;
  int own_id_;
  int other_id_;
  int own_val_ = 0;
  int other_val_ = 0;
};

}  // namespace write_skew
}  // namespace slog

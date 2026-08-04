#include <string>

#include "execution/execution.h"
#include "execution/write_skew/transaction.h"

namespace slog {

using std::stoi;

WriteSkewExecution::WriteSkewExecution(const SharderPtr& sharder, const std::shared_ptr<Storage>& storage)
    : sharder_(sharder), storage_(storage) {}

void WriteSkewExecution::Execute(Transaction& txn) {
  auto txn_adapter = std::make_shared<write_skew::TxnStorageAdapter>(txn);

  if (txn.code().procedures().empty() || txn.code().procedures(0).args().empty()) {
    txn.set_status(TransactionStatus::ABORTED);
    txn.set_abort_reason("Invalid code");
    return;
  }

  const auto& args = txn.code().procedures(0).args();
  const auto& txn_name = args[0];

  if (txn_name == "write_skew") {
    if (args.size() != 3) {
      txn.set_status(TransactionStatus::ABORTED);
      txn.set_abort_reason("WriteSkew Txn - Invalid number of arguments");
      return;
    }
    int own_id = stoi(args[1]);
    int other_id = stoi(args[2]);

    write_skew::WriteSkewTxn write_skew_txn(txn_adapter, own_id, other_id);
    if (!write_skew_txn.Execute()) {
      txn.set_status(TransactionStatus::ABORTED);
      txn.set_abort_reason("WriteSkew Txn - " + write_skew_txn.error());
      return;
    }
  } else {
    txn.set_status(TransactionStatus::ABORTED);
    txn.set_abort_reason("Unknown procedure name");
    return;
  }
  txn.set_status(TransactionStatus::COMMITTED);
  ApplyWrites(txn, sharder_, storage_);
}

}  // namespace slog

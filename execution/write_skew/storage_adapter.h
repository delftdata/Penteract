#pragma once

#include <utility>
#include <vector>

#include "common/types.h"
#include "proto/transaction.pb.h"
#include "storage/metadata_initializer.h"
#include "storage/storage.h"

namespace slog {
namespace write_skew {

class StorageAdapter {
 public:
  virtual ~StorageAdapter() = default;
  virtual const std::string* Read(const std::string& key) = 0;
  // Returns true if insertion succeeds
  virtual bool Insert(const std::string& key, std::string&& value) = 0;
  // Returns true if key exists before updating
  virtual bool Update(const std::string& key, std::function<void(std::string&)>&& update_fn) = 0;
  virtual bool Delete(std::string&& key) = 0;
  virtual std::vector<std::pair<std::string, std::string>> ScanPrefix(const std::string& prefix) { return {}; }
};

using StorageAdapterPtr = std::shared_ptr<StorageAdapter>;

class KVStorageAdapter : public StorageAdapter {
 public:
  KVStorageAdapter(const std::shared_ptr<Storage>& storage,
                   const std::shared_ptr<MetadataInitializer>& metadata_initializer);
  // This Read method is leaky. Only used for testing
  const std::string* Read(const std::string&) override;
  bool Insert(const std::string& key, std::string&& value) override;
  std::vector<std::pair<std::string, std::string>> ScanPrefix(const std::string& prefix) override;
  bool Update(const std::string& key, std::function<void(std::string&)>&& update_fn) override;
  bool Delete(std::string&&) override { throw std::runtime_error("Delete is unimplemented in KVStorageAdapter"); }

 private:
  std::shared_ptr<Storage> storage_;
  std::shared_ptr<MetadataInitializer> metadata_initializer_;
  std::vector<std::string> buffer_;
};

class TxnStorageAdapter : public StorageAdapter {
 public:
  TxnStorageAdapter(Transaction& txn);
  const std::string* Read(const std::string& key) override;
  bool Insert(const std::string& key, std::string&& value) override;
  bool Update(const std::string& key, std::function<void(std::string&)>&& update_fn) override;
  bool Delete(std::string&& key) override;

 private:
  void CheckIndexSize();
  Transaction& txn_;
  std::unordered_map<std::string, int> key_index_;
};

}  // namespace write_skew
}  // namespace slog

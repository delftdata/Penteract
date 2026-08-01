#include "execution/smallbank/metadata_initializer.h"

#include <glog/logging.h>

#include <functional>  // For std::hash
namespace slog {
namespace smallbank {

SmallBankMetadataInitializer::SmallBankMetadataInitializer(uint32_t num_regions, uint32_t num_partitions)
    : num_regions_(num_regions), num_partitions_(num_partitions) {}

int extractNumber(const std::string& client_name) {
  // Find the first digit (skip the "Client" part)
  std::string number_str = client_name.substr(6);                    // Start from character 6 to skip "Client"
  number_str = number_str.substr(0, number_str.find_first_of(" "));  // Remove any trailing spaces
  return std::stoi(number_str);                                      // Convert the number string to an integer
}

Metadata SmallBankMetadataInitializer::Compute(const Key& key) {
  if (key.size() == 26) {
    std::string client_name(reinterpret_cast<const char*>(key.data()), 24);
    int client_number = extractNumber(client_name);
    return Metadata(client_number % num_regions_);
  } else {
    uint32_t client_id = *reinterpret_cast<const uint32_t*>(key.data());
    return Metadata(client_id % num_regions_);
  }
}

}  // namespace smallbank
}  // namespace slog
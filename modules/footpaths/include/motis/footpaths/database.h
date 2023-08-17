#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "geo/latlng.h"

#include "lmdb/lmdb.hpp"

#include "motis/footpaths/matching.h"
#include "motis/footpaths/platforms.h"
#include "motis/footpaths/transfers.h"
#include "motis/footpaths/types.h"

#include "nigiri/keys.h"

namespace motis::footpaths {

inline string to_key(geo::latlng const& coord) {
  return {nigiri::to_location_key(coord)};
}

struct database {
  explicit database(std::string const& path, std::size_t const max_size);

  std::vector<std::size_t> put_platforms(platforms&);
  platforms get_platforms();
  hash_map<string, platform> get_platforms_with_key();
  platforms get_matched_platforms();

  std::vector<std::size_t> put_matching_results(matching_results const&);
  hash_map<string, platform> get_loc_to_pf_matchings();

  std::vector<std::size_t> put_transfer_requests_keys(
      transfer_requests_keys const&);
  transfer_requests_keys get_transfer_requests_keys();

  std::vector<std::size_t> put_transfer_results(transfer_results const&);
  std::vector<std::size_t> update_transfer_results(transfer_results const&);
  transfer_results get_transfer_results();

private:
  static lmdb::txn::dbi platforms_dbi(
      lmdb::txn&, lmdb::dbi_flags flags = lmdb::dbi_flags::NONE);
  static lmdb::txn::dbi matchings_dbi(
      lmdb::txn&, lmdb::dbi_flags flags = lmdb::dbi_flags::NONE);
  static lmdb::txn::dbi transreqs_dbi(
      lmdb::txn&, lmdb::dbi_flags flags = lmdb::dbi_flags::NONE);
  static lmdb::txn::dbi transfers_dbi(
      lmdb::txn&, lmdb::dbi_flags flags = lmdb::dbi_flags::NONE);

  void init();

  std::vector<std::pair<string, string>> get_matchings();
  std::optional<platform> get_platform(string const& /* osm_key */);

  lmdb::env mutable env_;
  std::mutex mutex_;
  std::int32_t highest_platform_id_{};
};

}  // namespace motis::footpaths

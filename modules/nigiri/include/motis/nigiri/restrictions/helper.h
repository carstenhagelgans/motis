#pragma once

#include <string_view>

#include "nigiri/timetable.h"
#include "nigiri/types.h"

namespace motis::nigiri::restrictions {

// Returns a hashmap that maps station names to their unique nigiri location
// index. Only stations that have been identified as parent stations in the
// timetable (given as argument) are considered.
::nigiri::hash_map<std::string_view, ::nigiri::location_idx_t>
get_parent_location_name_to_idx_mapping(::nigiri::timetable&);

}  // namespace motis::nigiri::restrictions

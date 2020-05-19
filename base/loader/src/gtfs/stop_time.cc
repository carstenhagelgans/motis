#include "motis/loader/gtfs/stop_time.h"

#include <algorithm>
#include <tuple>

#include "utl/enumerate.h"
#include "utl/parser/arg_parser.h"
#include "utl/parser/csv.h"

#include "motis/core/common/logging.h"
#include "motis/loader/gtfs/common.h"
#include "motis/loader/util.h"

using std::get;
using namespace utl;

namespace motis::loader::gtfs {

using gtfs_stop_time = std::tuple<cstr, cstr, cstr, cstr, int, cstr, int, int>;
enum {
  trip_id,
  arrival_time,
  departure_time,
  stop_id,
  stop_sequence,
  stop_headsign,
  pickup_type,
  drop_off_type
};

static const column_mapping<gtfs_stop_time> stop_time_columns = {
    {"trip_id", "arrival_time", "departure_time", "stop_id", "stop_sequence",
     "stop_headsign", "pickup_type", "drop_off_type"}};

int hhmm_to_min(cstr s) {
  if (s.len == 0) {
    return -1;
  } else {
    int hours = 0;
    parse_arg(s, hours, 0);
    if (s) {
      ++s;
    } else {
      return -1;
    }

    int minutes = 0;
    parse_arg(s, minutes, 0);

    return hours * 60 + minutes;
  }
}

void read_stop_times(loaded_file const& file, trip_map& trips,
                     stop_map const& stops) {
  motis::logging::scoped_timer timer{"read stop times"};
  std::clog << '\0' << 'S' << "Parse Stop Times" << '\0';

  std::string last_trip_id;
  trip* last_trip = nullptr;

  auto const entries = read<gtfs_stop_time>(file.content(), stop_time_columns);
  motis::logging::clog_set_progress_bounds(20, 40, entries.size());
  for (auto const& [i, s] : utl::enumerate(entries)) {
    motis::logging::clog_update_progress_int(i, 10000);

    trip* t = nullptr;
    auto t_id = get<trip_id>(s).to_str();
    if (last_trip != nullptr && t_id == last_trip_id) {
      t = last_trip;
    } else {
      t = trips.at(t_id).get();
      last_trip_id = t_id;
      last_trip = t;
    }

    t->stop_times_.emplace(
        get<stop_sequence>(s),  // index
        stops.at(parse_stop_id(get<stop_id>(s).to_str()))
            .get(),  // constructor arguments
        get<stop_headsign>(s).to_str(),  //
        hhmm_to_min(get<arrival_time>(s)), get<drop_off_type>(s) == 0,
        hhmm_to_min(get<departure_time>(s)), get<pickup_type>(s) == 0);
  }
}

}  // namespace motis::loader::gtfs

#include <utility>

#include "boost/algorithm/string.hpp"

#include "motis/core/common/logging.h"

#include "motis/footpaths/platforms.h"

#include "osmium/area/assembler.hpp"
#include "osmium/area/multipolygon_manager.hpp"
#include "osmium/geom/coordinates.hpp"
#include "osmium/handler/node_locations_for_ways.hpp"
#include "osmium/index/map/flex_mem.hpp"
#include "osmium/visitor.hpp"

#include "utl/pipes.h"

using namespace motis::logging;

namespace pprr = ppr::routing;

using index_type = osmium::index::map::FlexMem<osmium::unsigned_object_id_type,
                                               osmium::Location>;
using location_handler_type = osmium::handler::NodeLocationsForWays<index_type>;

namespace motis::footpaths {

osmium::geom::Coordinates calc_center(osmium::NodeRefList const& nr_list) {
  osmium::geom::Coordinates c{0.0, 0.0};

  for (auto const& nr : nr_list) {
    c.x += nr.lon();
    c.y += nr.lat();
  }

  c.x /= nr_list.size();
  c.y /= nr_list.size();

  return c;
}

struct platform_handler : public osmium::handler::Handler {
  explicit platform_handler(std::vector<platform>& platforms,
                            osmium::TagsFilter filter)
      : platforms_(platforms), filter_(std::move(filter)){};

  void node(osmium::Node const& node) {
    auto const& tags = node.tags();
    if (osmium::tags::match_any_of(tags, filter_)) {
      add_platform(osm_type::kNode, node.id(), node.location(), tags);
    }
  }

  void way(osmium::Way const& way) {
    auto const& tags = way.tags();
    if (osmium::tags::match_any_of(tags, filter_)) {
      add_platform(osm_type::kWay, way.id(), way.envelope().bottom_left(),
                   tags);
    }
  }

  void area(osmium::Area const& area) {
    auto const& tags = area.tags();
    if (osmium::tags::match_any_of(tags, filter_)) {
      add_platform(area.from_way() ? osm_type::kWay : osm_type::kRelation,
                   area.orig_id(),
                   calc_center(*area.cbegin<osmium::OuterRing>()), tags);
    }
  }

  unsigned int unique_platforms_{0};

private:
  void add_platform(osm_type const type, osmium::object_id_type const id,
                    osmium::geom::Coordinates const& coord,
                    osmium::TagList const& tags) {
    auto names = extract_platform_names(tags);

    if (!names.empty()) {
      ++unique_platforms_;
    }

    for (auto const& name : names) {
      platforms_.emplace_back(
          platform{0, geo::latlng{coord.y, coord.x},
                   platform_info{name, id, nigiri::location_idx_t::invalid(),
                                 type, platform_is_bus_stop(tags)}});
    }
  }

  std::vector<platform>& platforms_;
  osmium::TagsFilter filter_;
};

std::vector<platform> extract_osm_platforms(std::string const& osm_file) {

  scoped_timer const timer("Extract OSM Tracks from " + osm_file);

  osmium::io::File const input_file{osm_file};

  osmium::area::Assembler::config_type assembler_config;
  assembler_config.create_empty_areas = false;
  osmium::area::MultipolygonManager<osmium::area::Assembler> mp_manager{
      assembler_config};

  osmium::TagsFilter filter{false};
  filter.add_rule(true, "public_transport", "platform");
  filter.add_rule(true, "railway", "platform");

  {
    scoped_timer const timer("Extract OSM tracks: Pass 1...");
    osmium::relations::read_relations(input_file, mp_manager);
  }

  index_type index;
  location_handler_type location_handler{index};
  std::vector<platform> platforms;
  platform_handler data_handler{platforms, filter};

  {
    scoped_timer const timer("Extract OSM tracks: Pass 2...");

    osmium::io::Reader reader{input_file, osmium::io::read_meta::no};
    osmium::apply(
        reader, location_handler, data_handler,
        mp_manager.handler(
            [&data_handler](const osmium::memory::Buffer& area_buffer) {
              osmium::apply(area_buffer, data_handler);
            }));

    reader.close();
  }

  LOG(info) << "Extracted " << data_handler.unique_platforms_
            << " unique platforms from OSM.";
  LOG(info) << "Generated " << platforms.size() << " platform_info structs. "
            << static_cast<float>(platforms.size()) /
                   static_cast<float>(data_handler.unique_platforms_)
            << " entries per platform.";

  return platforms;
}

std::vector<std::string> extract_platform_names(osmium::TagList const& tags) {
  std::vector<std::string> platform_names;

  auto add_names = [&](const std::string& name_by_tag) {
    platform_names.emplace_back(name_by_tag);
    return;
    /**
     * // In case matching is invalid: try to split names; currently not needed
     *
     * std::vector<std::string> names{};
     * boost::split(names, name_by_tag,
     *              [](char c) { return c == ';' || c == '/'; });
     *
     * if (std::any_of(names.begin(), names.end(),
     *   [&](std::string const& name) -> bool {
     *     return name.length() > 3;
     *    })) {
     *       platform_names.emplace_back(name_by_tag);
     *       return;
     *     }
     *
     * for (auto const& name : names) {
     *   platform_names.emplace_back(name);
     * }
     */
  };

  // REMOVE *.clear() to get more names for matching
  // TODO (Carsten) find a better way of matching
  if (tags.has_key("name")) {
    platform_names.clear();
    add_names(tags.get_value_by_key("name"));
  }
  if (tags.has_key("description")) {
    platform_names.clear();
    add_names(tags.get_value_by_key("description"));
  }
  if (tags.has_key("ref_name")) {
    platform_names.clear();
    add_names(tags.get_value_by_key("ref_name"));
  }
  if (tags.has_key("local_ref")) {
    platform_names.clear();
    add_names(tags.get_value_by_key("local_ref"));
  }
  if (tags.has_key("ref")) {
    platform_names.clear();
    add_names(tags.get_value_by_key("ref"));
  }

  return platform_names;
}

bool platform_is_bus_stop(osmium::TagList const& tags) {
  return (tags.has_key("highway") &&
          strcmp(tags.get_value_by_key("highway"), "bus_stop") == 0);
}

pprr::osm_namespace to_ppr_osm_type(osm_type const& t) {
  switch (t) {
    case osm_type::kNode: return pprr::osm_namespace::NODE;
    case osm_type::kWay: return pprr::osm_namespace::WAY;
    case osm_type::kRelation: return pprr::osm_namespace::RELATION;
    default: return pprr::osm_namespace::NODE;
  }
}

pprr::input_location to_input_location(platform const& pf) {
  pprr::input_location il;
  // TODO (Carsten) OSM_ELEMENT LEVEL missing
  il.osm_element_ = {pf.info_.osm_id_, to_ppr_osm_type(pf.info_.osm_type_)};
  il.location_ = ::ppr::make_location(pf.loc_.lng_, pf.loc_.lat_);
  return il;
}

std::vector<platform*> platforms_index::get_valid_platforms_in_radius(
    platform const* pf, double const r) {
  return utl::all(platform_index_.in_radius(pf->loc_, r)) |
         utl::transform([this](std::size_t i) { return get_platform(i); }) |
         utl::remove_if([&](auto* target_platform) {
           return target_platform->info_.idx_ ==
                      nigiri::location_idx_t::invalid() ||
                  target_platform->info_.idx_ == pf->info_.idx_;
         }) |
         utl::vec();
}

std::vector<platform*> platforms_index::get_platforms_in_radius(
    geo::latlng const loc, double const r) {
  return utl::all(platform_index_.in_radius(loc, r)) |
         utl::transform([this](std::size_t i) { return get_platform(i); }) |
         utl::vec();
}

}  // namespace motis::footpaths

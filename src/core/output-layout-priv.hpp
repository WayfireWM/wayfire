#include <wayfire/output-layout.hpp>
#include <string_view>

#include <xf86drmMode.h>

namespace wf
{
namespace layout_detail
{
void priv_output_layout_fini(wf::output_layout_t *layout);
std::string_view get_output_source_name(output_image_source_t source);
std::string wl_transform_to_string(wl_output_transform transform);
wl_output_transform get_transform_from_string(std::string_view transform);
bool parse_modeline(const char *modeline, drmModeModeInfo& mode);

wf::geometry_t calculate_output_geometry(const output_state_t& state);
std::vector<wf::geometry_t> calculate_fixed_geometries(const output_configuration_t& config);
bool has_overlapping_outputs(const output_configuration_t& config);
bool all_outputs_disabled(const output_configuration_t& config);
bool are_rectangles_touching(const wf::geometry_t& a, const wf::geometry_t& b);
bool has_disjoint_outputs(const output_configuration_t& config);
}
}

#pragma once

#include "wayfire/signal-definitions.hpp"
#include "wayfire/signal-provider.hpp"
#include <string>

#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/plugins/scale-signal.hpp>

namespace wf
{
namespace scene
{
class title_overlay_node_t;
}
}


class scale_show_title_t
{
  protected:
    /* Overlays for showing the title of each view */
    wf::option_wrapper_t<wf::color_t> bg_color{"scale/bg_color"};
    wf::option_wrapper_t<wf::color_t> text_color{"scale/text_color"};
    wf::option_wrapper_t<std::string> show_view_title_overlay_opt{
        "scale/title_overlay"};
    wf::option_wrapper_t<int> title_font_size{"scale/title_font_size"};
    wf::option_wrapper_t<std::string> title_position{"scale/title_position"};
    wf::output_t *output;

  public:
    scale_show_title_t();

    void init(wf::output_t *output);

    void fini();

  protected:
    /* signals */
    wf::signal::connection_t<scale_filter_signal> view_filter;
    wf::signal::connection_t<scale_end_signal> scale_end;
    wf::signal::connection_t<scale_transformer_added_signal> add_title_overlay;
    wf::signal::connection_t<scale_transformer_removed_signal> rem_title_overlay;
    wf::signal::connection_t<wf::post_input_event_signal<wlr_pointer_motion_event>> post_motion;
    wf::signal::connection_t<wf::post_input_event_signal<wlr_pointer_motion_absolute_event>>
    post_absolute_motion;

    enum class title_overlay_t
    {
        NEVER,
        MOUSE,
        ALL,
    };

    friend class wf::scene::title_overlay_node_t;

    title_overlay_t show_view_title_overlay;
    /* only used if title overlay is set to follow the mouse */
    wayfire_view last_title_overlay = nullptr;

    void update_title_overlay_opt();
    void update_title_overlay_mouse();
};

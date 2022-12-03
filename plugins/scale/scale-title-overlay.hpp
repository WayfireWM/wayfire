#pragma once

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
    wf::signal_connection_t view_filter;
    wf::signal_connection_t scale_end;
    wf::signal_connection_t add_title_overlay;
    wf::signal_connection_t rem_title_overlay;
    wf::signal_connection_t mouse_update;

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

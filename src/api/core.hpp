#ifndef FIRE_H
#define FIRE_H

#include <functional>
#include <memory>
#include <vector>
#include <map>

extern "C"
{
#include <wlr/backend.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_output_layout.h>

#define static
#include <wlr/types/wlr_compositor.h>
#undef static

struct wlr_data_device_manager;
struct wlr_linux_dmabuf;
struct wlr_gamma_control_manager;
struct wlr_screenshooter;
struct wlr_xdg_output_manager;
struct wayfire_shell;

#include <wayland-server.h>
}

class decorator_base_t;
class input_manager;
class wayfire_config;
class wayfire_output;
class wayfire_view_t;
class wayfire_surface_t;

using wayfire_view = std::shared_ptr<wayfire_view_t>;
using output_callback_proc = std::function<void(wayfire_output *)>;

class wayfire_core
{
        friend struct plugin_manager;
        friend class wayfire_output;

        wl_listener output_layout_changed;

        wayfire_output *active_output;

        std::vector<wlr_output*> pending_outputs;
        std::map<wlr_output*, wayfire_output*> outputs;

        /* TODO: perhaps switch to a std::vector */
        std::map<wayfire_view_t*, wayfire_view> views;

        void configure(wayfire_config *config);

        int times_wake = 0;
        uint32_t focused_layer = 0;

    public:

        std::vector<wl_resource*> shell_clients;
        wayfire_config *config;

        bool set_decorator(decorator_base_t *decor);

        wl_display *display;
        wl_event_loop *ev_loop;
        wlr_backend *backend;
        wlr_renderer *renderer;
        wlr_output_layout *output_layout;
        wlr_compositor *compositor;

        struct
        {
            wlr_data_device_manager *data_device;
            wlr_gamma_control_manager *gamma;
            wlr_screenshooter *screenshooter;
            wlr_linux_dmabuf *linux_dmabuf;
            wlr_xdg_output_manager *output_manager;
            wayfire_shell *wf_shell;
        } protocols;


        std::string wayland_display, xwayland_display;

        input_manager *input;

        void init(wayfire_config *config);
        void wake();
        void sleep();
        void refocus_active_output_active_view();

        wlr_seat *get_current_seat();
        void set_default_cursor();

        /* in output-layout-local coordinates */
        std::tuple<int, int> get_cursor_position();

        /* in output-layout-local coordinates */
        std::tuple<int, int> get_touch_position(int id);

        wayfire_surface_t *get_cursor_focus();
        wayfire_surface_t *get_touch_focus();

        void add_view(wayfire_view view);
        wayfire_view find_view(wayfire_surface_t *);
        wayfire_view find_view(uint32_t id);

        /* completely destroy a view */
        void erase_view(wayfire_view view);

        /* brings the view to the top
         * and also focuses its output */
        void focus_view(wayfire_view win, wlr_seat *seat);
        void move_view_to_output(wayfire_view v, wayfire_output *new_output);

        void add_output(wlr_output *output);
        wayfire_output *get_output(wlr_output *output);
        wayfire_output *get_output(std::string name);

        void focus_output(wayfire_output *o);
        void remove_output(wayfire_output *o);

        wayfire_output *get_active_output();
        wayfire_output *get_next_output(wayfire_output *output);
        wayfire_output *get_output_at(int x, int y);
        size_t          get_num_outputs();

        void for_each_output(output_callback_proc);

        void focus_layer(uint32_t layer);
        uint32_t get_focused_layer();

        void run(const char *command);

        int vwidth, vheight;

        std::string shadersrc;
        bool run_panel;
};

extern wayfire_core *core;
#endif

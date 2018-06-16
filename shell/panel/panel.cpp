#include <sstream>
#include <unistd.h>
#include <sys/time.h>
#include <linux/input-event-codes.h>
#include "panel.hpp"
#include "widgets.hpp"
#include "net.hpp"

void panel_redraw(void *data, wl_callback*, uint32_t)
{
    wayfire_panel *panel = (wayfire_panel*) data;
    panel->render_frame();
}

static const struct wl_callback_listener frame_listener = {
    panel_redraw
};

static void load_misc_config(wayfire_config *config)
{
    auto section = config->get_section("shell_panel");

    widget::background_color = *section->get_option("background_color",
            "0.033 0.041 0.047 0.9");
    widget::font_size = *section->get_option("font_size", "25");
    widget::font_face = section->get_option("font_face",
            "/usr/share/fonts/gnu-free/FreeSerif.ttf")->as_string();

    battery_options::icon_path_prefix = section->get_option("battery_icon_path_prefix",
            "/usr/share/icons/Adwaita/64x64/status")->as_string();
    battery_options::invert_icons = int(*section->get_option("battery_invert_icons", "1"));
    battery_options::text_scale = double(*section->get_option("battery_text_scale", "0.6"));
}

void zwf_output_hide_panels(void *data,
                            struct zwf_output_v1 *zwf_output_v1,
                            uint32_t autohide)
{
    auto panel = (wayfire_panel*) data;
    panel->set_autohide(autohide);
}

const struct zwf_output_v1_listener zwf_output_impl =
{
    zwf_output_hide_panels
};

wayfire_panel::wayfire_panel(wayfire_config *config, wayfire_output *output)
{
    this->config = config;
    this->output = output;
    load_misc_config(config);

    autohide_opt = (*config)["shell_panel"]->get_option("autohide", "1");
    autohide = (bool)autohide_opt->as_int();

    output->resized_callback = [=] (wayfire_output *output, int w, int h)
    { init(w, h); };

    output->destroyed_callback = [=] (wayfire_output *output)
    { delete this; };

    zwf_output_v1_add_listener(output->zwf, &zwf_output_impl, this);
}

wayfire_panel::~wayfire_panel()
{
    destroy();
}

void wayfire_panel::init(int w, int h)
{
    width = w * output->scale;
    height = 1.3 * widget::font_size;

    zwf_output_v1_inhibit_output(output->zwf);

    if (window)
        destroy();

    window = output->create_window(width, height, [=] ()
                                   {
        configure();
    });
}

void wayfire_panel::destroy()
{
    for_each_widget(w)
    {
        cairo_destroy(w->cr);
        delete w;
    }

    cairo_destroy(cr);
    delete window;

    for (int i = 0; i < 3; i++)
        widgets[i].clear();
}

void wayfire_panel::configure()
{
    window->zwf = zwf_output_v1_get_wm_surface(output->zwf, window->surface,
                                               ZWF_OUTPUT_V1_WM_ROLE_PANEL);

    init_input();
    init_widgets();

    cr = cairo_create(window->cairo_surface);

    if (repaint_callback)
        wl_callback_destroy(repaint_callback);
    repaint_callback = nullptr;

    if (!autohide)
        zwf_wm_surface_v1_set_exclusive_zone(window->zwf, ZWF_WM_SURFACE_V1_ANCHOR_EDGE_TOP, height / output->scale);

    zwf_wm_surface_v1_configure(window->zwf, 0, -height);

    state = HIDDEN;
    animation.y = -height;
    show(0);

    render_frame(true);
    zwf_output_v1_inhibit_output_done(output->zwf);
}

void wayfire_panel::init_input()
{
    using namespace std::placeholders;
    window->pointer_enter = [=] (wl_pointer*, uint32_t time, int, int)
    {
        show(200);
        on_enter(time);
        ++count_input;
    };

    window->pointer_leave = [=] ()
    {
        --count_input;
        if (!count_input)
        {
            if (autohide)
                hide(300);
            on_leave();
        }
    };

    window->pointer_move  = std::bind(std::mem_fn(&wayfire_panel::on_motion), this, _1, _2);
    window->pointer_button= std::bind(std::mem_fn(&wayfire_panel::on_button), this, _1, _2, _3, _4);

    window->touch_down = [=] (uint32_t time, int32_t id, int x, int y)
    {
        ++count_input;
        if (id == 0)
        {
            show(0);
            on_enter(time);

            on_button(BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED, x, y);
            on_motion(x, y);
            last_x = x;
            last_y = y;
        }
    };

    window->touch_up = [=] (int32_t id)
    {
        --count_input;
        if (id == 0)
        {
            on_button(BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED, last_x, last_y);
            on_motion(-1, -1);
        }

        if (count_input == 0)
        {
            if (autohide)
                hide(1000);
            on_leave();
        }
    };

    window->touch_motion = [=] (int32_t id, int x, int y)
    {
        if (id == 0)
        {
            last_x = x;
            last_y = y;
            on_motion(x, y);
        }
    };
}

void wayfire_panel::position_widgets(position_policy policy)
{
    int widget_spacing = widget::font_size * 0.5;
    int total_width = widget_spacing;

    for (size_t i = 0; i < widgets[policy].size(); i++)
    {
        widgets[policy][i]->x = total_width;
        total_width += widgets[policy][i]->get_width();
        total_width += widget_spacing;
    }

    int delta = 0;
    if (policy == PART_RIGHT)
        delta += width - total_width;
    if (policy == PART_SYMMETRIC)
        delta += width / 2 - total_width / 2;

    for (size_t i = 0; i < widgets[policy].size(); i++)
        widgets[policy][i]->x += delta;
}

widget* wayfire_panel::create_widget_from_name(std::string name)
{
    widget *w = nullptr;
    if (name == "clock")
    {
        w = new clock_widget();
    }
    else if (name == "battery")
    {
        w = new battery_widget();
    }
    else if (name == "launchers")
    {
        auto l = new launchers_widget();
        l->init_launchers(config);
        w = l;
    }
    else if (name == "network")
    {
        w = new network_widget();
    }

    if (w)
    {
        w->cr = cairo_create(window->cairo_surface);
        w->panel_h = height;
        w->create();
    }

    return w;
}

void wayfire_panel::init_widgets(std::string str, position_policy policy)
{
    std::istringstream stream(str);
    std::string name;
    while(stream >> name)
    {
        auto w = create_widget_from_name(name);
        if (w) widgets[policy].push_back(w);
    }

    position_widgets(policy);
}

void wayfire_panel::init_widgets()
{
    cr = cairo_create(window->cairo_surface);

    auto section = config->get_section("shell_panel");
    std::string left   = *section->get_option("widgets_left", "");
    std::string center = *section->get_option("widgets_center", "clock");
    std::string right  = *section->get_option("widgets_right", "");

    init_widgets(left, PART_LEFT);
    init_widgets(center, PART_SYMMETRIC);
    init_widgets(right, PART_RIGHT);
}

void wayfire_panel::set_autohide(bool ah)
{
    autohide += ah ? 1 : -1;

    if (autohide == 0)
    {
        show(0);
        on_enter(0);
    } else if (!count_input)
    {
        hide(0);
        on_leave();
    }
}

static inline void set_timer_target(timeval& timer, int delay)
{
    gettimeofday(&timer, 0);
    timer.tv_usec += delay * 1000;

    const int usec_in_sec = 1000000;
    timer.tv_sec += timer.tv_usec / usec_in_sec;
    timer.tv_usec %= usec_in_sec;
}

void wayfire_panel::show(int delay)
{
    if (animation.dy <= 0)
    {
        animation.target = 0;
        animation.dy = 5;
    }

    if (state & SHOWN)
    {
        state = HIDDEN | ANIMATING;
        return;
    } else if (!(state & WAITING))
    {
        state = HIDDEN | WAITING;
        set_timer_target(timer_target, delay);
        add_callback(false);
    }
}

void wayfire_panel::hide(int delay)
{
    if (animation.dy >= 0)
    {
        animation.target = hidden_height - height;
        animation.dy = -5;
    }

    if (state & HIDDEN)
    {
        if (state == (HIDDEN | WAITING))
            state = HIDDEN;
        else
            state = SHOWN | ANIMATING;
        return;
    } else if (!(state & WAITING))
    {
        state = SHOWN | WAITING;
        set_timer_target(timer_target, delay);
        add_callback(false);
    }
}

void wayfire_panel::on_enter(uint32_t serial)
{
    output->display->show_default_cursor(serial);
    add_callback(false);
}

void wayfire_panel::on_leave()
{
    on_motion(-1, -1);
}

void wayfire_panel::on_button(uint32_t button, uint32_t state, int x, int y)
{
    for_each_widget(w)
    {
        if (w->pointer_button)
            w->pointer_button(button, state, x, y);
    }
}

void wayfire_panel::on_motion(int x, int y)
{
    for_each_widget(w)
    {
        if (w->pointer_motion)
            w->pointer_motion(x, y);
    }
}

void wayfire_panel::add_callback(bool swapped)
{
    if (repaint_callback)
        wl_callback_destroy(repaint_callback);

    repaint_callback = wl_surface_frame(window->surface);
    wl_callback_add_listener(repaint_callback, &frame_listener, this);

    if (!swapped)
        wl_surface_commit(window->surface);
}


void wayfire_panel::render_frame(bool first_call)
{
    if (state & WAITING)
    {
        timeval time;
        gettimeofday(&time, 0);

        long long delta = (timer_target.tv_sec - time.tv_sec) * 1e6 +
            (timer_target.tv_usec - time.tv_usec);

        if (delta <= 0)
        {
            state &= ~WAITING;
            state |= ANIMATING;
        }
    }

    if (state & ANIMATING) {
        animation.y += animation.dy;

        if (animation.y * animation.dy > animation.target * animation.dy)
        {
            animation.y = animation.target;
            if (state & HIDDEN)
            {
                state = SHOWN;

                if (!count_input && autohide)
                    hide(300);
            }
            else
            {
                state = HIDDEN;
            }
        }

        zwf_wm_surface_v1_configure(window->zwf, 0, animation.y);
    }

    bool should_swap = first_call;
    if (animation.target == 0 || !autohide)
    {
        for_each_widget(w)
            should_swap |= w->update();
    }

    should_swap = should_swap || need_fullredraw;

    if (should_swap)
    {
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        render_rounded_rectangle(cr, 0, 0, width, height,
                0, widget::background_color.r, widget::background_color.g,
                widget::background_color.b, widget::background_color.a);
        need_fullredraw = false;

        for (int i = 0; i < 3; i++)
            position_widgets((position_policy) i);

        for_each_widget(w)
            w->repaint();
    }

    /* we don't need to redraw only if we are autohiding and hidden now */
    if (state != HIDDEN)
        add_callback(should_swap);

    if (should_swap)
        window->damage_commit();

    /* don't repaint too often if there is no interaction with the panel,
     * otherwise the panel eats up some CPU power */
    if (((state & (WAITING | ANIMATING)) == 0) && count_input <= 0)
        usleep(1e6/10);
}

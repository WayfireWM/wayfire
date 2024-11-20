#include <fcntl.h>
#include <unistd.h>
#include <set>
#include <wayfire/plugin.hpp>
#include <wayfire/core.hpp>
#include <wayfire/unstable/wlr-text-input-v3-popup.hpp>
#include "input-method-unstable-v1-protocol.h"
#include "wayfire/option-wrapper.hpp"
#include "wayfire/nonstd/wlroots-full.hpp"
#include "wayfire/nonstd/wlroots.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/seat.hpp"

#include "text-input-v1-v3.hpp"
#include "input-method-v1.hpp"

class wayfire_input_method_v1_context
{
  public:
    wayfire_input_method_v1_context(wayfire_im_text_input_base_t *text_input, wl_resource *current_im,
        const struct zwp_input_method_context_v1_interface *context_impl)
    {
        this->text_input = text_input;
        this->current_im = current_im;
        context = wl_resource_create(wl_resource_get_client(current_im),
            &zwp_input_method_context_v1_interface, 1, 0);
        wl_resource_set_implementation(context, context_impl, this, handle_ctx_destruct_final);
        zwp_input_method_v1_send_activate(current_im, context);
    }

    static void handle_ctx_destruct_final(wl_resource *resource)
    {
        auto context = static_cast<wayfire_input_method_v1_context*>(wl_resource_get_user_data(resource));
        if (context)
        {
            context->deactivate(true);
        }
    }

    void handle_text_input_v3_commit()
    {
        auto ti_v3 = dynamic_cast<wayfire_im_v1_text_input_v3*>(text_input);
        wf::dassert(ti_v3, "handle_text_input_v3_commit called without text_input_v3");
        auto text_input_v3 = ti_v3->text_input_v3;
        zwp_input_method_context_v1_send_content_type(context,
            text_input_v3->current.content_type.hint, text_input_v3->current.content_type.purpose);
        zwp_input_method_context_v1_send_surrounding_text(context,
            text_input_v3->current.surrounding.text ?: "",
            text_input_v3->current.surrounding.cursor,
            text_input_v3->current.surrounding.anchor);
        zwp_input_method_context_v1_send_commit_state(context, ctx_serial++);
    }

    void deactivate(bool im_killed = false)
    {
        wl_resource_set_user_data(context, NULL);
        auto& seat = wf::get_core().seat;

        if (im_killed)
        {
            // Remove keys which core still thinks are pressed down physically: they will be sent as release
            // events to the client at a later point.
            for (auto& hw_pressed : seat->get_pressed_keys())
            {
                if (currently_pressed_keys_client.count(hw_pressed))
                {
                    currently_pressed_keys_client.erase(
                        currently_pressed_keys_client.find(hw_pressed));
                }
            }

            // For the other keys (where we potentially swallowed the release event, but the IM did not
            // respond yet with a release), release those keys.
            for (auto& key : currently_pressed_keys_client)
            {
                wlr_seat_keyboard_notify_key(seat->seat, wf::get_current_time(),
                    key, WL_KEYBOARD_KEY_STATE_RELEASED);
            }

            currently_pressed_keys_client.clear();
            if (active_grab_keyboard)
            {
                wl_resource_set_user_data(active_grab_keyboard, NULL);
            }

            this->text_input = NULL;
            return;
        }

        this->text_input = NULL;
        zwp_input_method_v1_send_deactivate(current_im, context);

        if (active_grab_keyboard)
        {
            for (auto& key : currently_pressed_keys_im)
            {
                wl_keyboard_send_key(active_grab_keyboard, vkbd_serial++, wf::get_current_time(),
                    key, WL_KEYBOARD_KEY_STATE_RELEASED);
            }

            currently_pressed_keys_im.clear();
            wl_resource_destroy(active_grab_keyboard);
        }
    }

    void handle_im_key(uint32_t time, uint32_t key, uint32_t state)
    {
        auto& seat = wf::get_core().seat;
        wlr_seat_keyboard_notify_key(seat->seat, time, key, state);
        update_pressed_keys(currently_pressed_keys_client, key, state);
    }

    void handle_im_modifiers(uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
        uint32_t mods_locked, uint32_t group)
    {
        wlr_keyboard_modifiers mods{
            .depressed = mods_depressed,
            .latched   = mods_latched,
            .locked    = mods_locked,
            .group     = group
        };

        auto& seat = wf::get_core().seat;
        wlr_seat_keyboard_notify_modifiers(seat->seat, &mods);
    }

    void grab_keyboard(wl_client *client, uint32_t id)
    {
        this->active_grab_keyboard = wl_resource_create(client, &wl_keyboard_interface, 1, id);
        wl_resource_set_implementation(active_grab_keyboard, NULL, this, unbind_keyboard);

        wf::get_core().connect(&on_keyboard_key);
        wf::get_core().connect(&on_keyboard_modifiers);
    }

    static void unbind_keyboard(wl_resource *keyboard)
    {
        auto self = static_cast<wayfire_input_method_v1_context*>(wl_resource_get_user_data(keyboard));
        if (!self)
        {
            return;
        }

        self->active_grab_keyboard = NULL;
        self->last_sent_keymap_keyboard = NULL;
        self->on_keyboard_key.disconnect();
        self->on_keyboard_modifiers.disconnect();
        self->currently_pressed_keys_im.clear();
    }

    void update_pressed_keys(std::multiset<uint32_t>& set, uint32_t key, uint32_t state)
    {
        if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
        {
            set.insert(key);
        } else if (set.count(key))
        {
            set.erase(set.find(key));
        }
    }

    wf::signal::connection_t<wf::pre_client_input_event_signal<wlr_keyboard_key_event>> on_keyboard_key =
        [=] (wf::pre_client_input_event_signal<wlr_keyboard_key_event> *ev)
    {
        if (active_grab_keyboard && !ev->carried_out)
        {
            check_send_keymap(wlr_keyboard_from_input_device(ev->device));
            ev->carried_out = true;
            wl_keyboard_send_key(active_grab_keyboard, vkbd_serial++, ev->event->time_msec,
                ev->event->keycode, ev->event->state);

            // Keep track of pressed keys so that we can release all of them at the end.
            // Otherwise the IM gets stuck thinking that some modifiers are pressed, etc.
            update_pressed_keys(currently_pressed_keys_im, ev->event->keycode, ev->event->state);
        }
    };

    wf::signal::connection_t<wf::input_event_signal<mwlr_keyboard_modifiers_event>> on_keyboard_modifiers =
        [=] (wf::input_event_signal<mwlr_keyboard_modifiers_event> *ev)
    {
        if (active_grab_keyboard)
        {
            auto kbd = wlr_keyboard_from_input_device(ev->device);
            check_send_keymap(kbd);
            wl_keyboard_send_modifiers(active_grab_keyboard, vkbd_serial++,
                kbd->modifiers.depressed, kbd->modifiers.latched, kbd->modifiers.locked,
                kbd->modifiers.group);
        }
    };

    void check_send_keymap(wlr_keyboard *current_kbd)
    {
        if (current_kbd == last_sent_keymap_keyboard)
        {
            return;
        }

        last_sent_keymap_keyboard = current_kbd;
        if (current_kbd->keymap != NULL)
        {
            wl_keyboard_send_keymap(active_grab_keyboard,
                WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, current_kbd->keymap_fd, current_kbd->keymap_size);
        } else
        {
            int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
            wl_keyboard_send_keymap(active_grab_keyboard,
                WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP, fd, 0);
            close(fd);
        }

        // Send new modifiers
        wl_keyboard_send_modifiers(active_grab_keyboard, vkbd_serial++,
            current_kbd->modifiers.depressed, current_kbd->modifiers.latched, current_kbd->modifiers.locked,
            current_kbd->modifiers.group);
    }

    std::multiset<uint32_t> currently_pressed_keys_im;
    std::multiset<uint32_t> currently_pressed_keys_client;

    wlr_keyboard *last_sent_keymap_keyboard = NULL;
    wl_resource *active_grab_keyboard = NULL;

    uint32_t ctx_serial  = 0;
    uint32_t vkbd_serial = 0;

    wl_resource *current_im = NULL;
    wl_resource *context    = NULL;

    // NULL if inactive
    wayfire_im_text_input_base_t *text_input = NULL;
};

void handle_im_context_destroy(wl_client *client, wl_resource *resource)
{
    wl_resource_destroy(resource);
}

void handle_im_context_commit_string(wl_client *client, wl_resource *resource,
    uint32_t serial, const char *text)
{
    auto context = static_cast<wayfire_input_method_v1_context*>(wl_resource_get_user_data(resource));
    if (context && context->text_input)
    {
        context->text_input->send_commit_string(serial, text);
    }
}

void handle_im_context_preedit_string(wl_client *client, wl_resource *resource,
    uint32_t serial, const char *text, const char *commit)
{
    auto context = static_cast<wayfire_input_method_v1_context*>(wl_resource_get_user_data(resource));
    if (context && context->text_input)
    {
        context->text_input->send_preedit_string(serial, text, commit);
    }
}

void handle_im_context_preedit_styling(wl_client *client, wl_resource *resource,
    uint32_t index, uint32_t length, uint32_t style)
{
    auto context = static_cast<wayfire_input_method_v1_context*>(wl_resource_get_user_data(resource));
    if (context && context->text_input)
    {
        context->text_input->send_preedit_styling(index, length, style);
    }
}

void handle_im_context_preedit_cursor(wl_client *client, wl_resource *resource, int32_t index)
{
    auto context = static_cast<wayfire_input_method_v1_context*>(wl_resource_get_user_data(resource));
    if (context && context->text_input)
    {
        context->text_input->send_preedit_cursor(index);
    }
}

void handle_im_context_delete_surrounding_text(wl_client *client, wl_resource *resource,
    int32_t index, uint32_t length)
{
    auto context = static_cast<wayfire_input_method_v1_context*>(wl_resource_get_user_data(resource));
    if (context && context->text_input)
    {
        context->text_input->send_delete_surrounding_text(index, length);
    }
}

void handle_im_context_cursor_position(wl_client *client, wl_resource *resource,
    int32_t index, int32_t anchor)
{
    auto context = static_cast<wayfire_input_method_v1_context*>(wl_resource_get_user_data(resource));
    if (context && context->text_input)
    {
        context->text_input->send_cursor_position(index, anchor);
    }
}

void handle_im_context_modifiers_map(wl_client *client, wl_resource *resource,
    struct wl_array *map)
{
    auto context = static_cast<wayfire_input_method_v1_context*>(wl_resource_get_user_data(resource));
    if (context && context->text_input)
    {
        context->text_input->send_modifiers_map(map);
    }
}

void handle_im_context_keysym(wl_client *client, wl_resource *resource,
    uint32_t serial, uint32_t time, uint32_t sym, uint32_t state,
    uint32_t modifiers)
{
    auto context = static_cast<wayfire_input_method_v1_context*>(wl_resource_get_user_data(resource));
    if (context && context->text_input)
    {
        context->text_input->send_keysym(serial, time, sym, state, modifiers);
    }
}

void handle_im_context_grab_keyboard(wl_client *client, wl_resource *resource,
    uint32_t keyboard_id)
{
    auto context = static_cast<wayfire_input_method_v1_context*>(wl_resource_get_user_data(resource));
    if (context)
    {
        context->grab_keyboard(client, keyboard_id);
    } else
    {
        // Create a dummy resource to avoid Wayland protocol errors.
        // But, we have already moved on from this context, so we won't send any events.
        auto resource = wl_resource_create(client, &wl_keyboard_interface, 1, keyboard_id);
        wl_resource_set_implementation(resource, NULL, NULL, NULL);
    }
}

void handle_im_context_key(wl_client*, wl_resource *resource,
    uint32_t, uint32_t time, uint32_t key, uint32_t state)
{
    auto context = static_cast<wayfire_input_method_v1_context*>(wl_resource_get_user_data(resource));
    if (context)
    {
        context->handle_im_key(time, key, state);
    }
}

void handle_im_context_modifiers(wl_client *client, wl_resource *resource,
    uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
    uint32_t mods_locked, uint32_t group)
{
    auto context = static_cast<wayfire_input_method_v1_context*>(wl_resource_get_user_data(resource));
    if (context)
    {
        context->handle_im_modifiers(serial, mods_depressed, mods_latched, mods_locked, group);
    }
}

void handle_im_context_language(wl_client *client, wl_resource *resource,
    uint32_t serial, const char *language)
{
    auto context = static_cast<wayfire_input_method_v1_context*>(wl_resource_get_user_data(resource));
    if (context && context->text_input)
    {
        context->text_input->send_language(serial, language);
    }
}

void handle_im_context_text_direction(wl_client *client, wl_resource *resource,
    uint32_t serial, uint32_t direction)
{
    auto context = static_cast<wayfire_input_method_v1_context*>(wl_resource_get_user_data(resource));
    if (context && context->text_input)
    {
        context->text_input->send_text_direction(serial, direction);
    }
}

static const struct zwp_input_method_context_v1_interface context_implementation = {
    .destroy = handle_im_context_destroy,
    .commit_string   = handle_im_context_commit_string,
    .preedit_string  = handle_im_context_preedit_string,
    .preedit_styling = handle_im_context_preedit_styling,
    .preedit_cursor  = handle_im_context_preedit_cursor,
    .delete_surrounding_text = handle_im_context_delete_surrounding_text,
    .cursor_position = handle_im_context_cursor_position,
    .modifiers_map   = handle_im_context_modifiers_map,
    .keysym = handle_im_context_keysym,
    .grab_keyboard = handle_im_context_grab_keyboard,
    .key = handle_im_context_key,
    .modifiers = handle_im_context_modifiers,
    .language  = handle_im_context_language,
    .text_direction = handle_im_context_text_direction
};


void handle_input_panel_get_input_panel_surface(wl_client *client, wl_resource *resource,
    uint32_t id, struct wl_resource *surface);
static const struct zwp_input_panel_v1_interface panel_implementation = {
    .get_input_panel_surface = handle_input_panel_get_input_panel_surface
};

void handle_input_panel_surface_set_toplevel(wl_client *client, wl_resource *resource,
    struct wl_resource *output, uint32_t position);
void handle_input_panel_surface_set_overlay_panel(wl_client *client, wl_resource *resource);

static const struct zwp_input_panel_surface_v1_interface panel_surface_implementation = {
    .set_toplevel = handle_input_panel_surface_set_toplevel,
    .set_overlay_panel = handle_input_panel_surface_set_overlay_panel
};

class wayfire_input_method_v1_panel_surface
{
  public:
    wayfire_input_method_v1_panel_surface(wl_client *client, uint32_t id,
        wf::text_input_v3_im_relay_interface_t *relay, wlr_surface *surface)
    {
        LOGC(IM, "Input method panel surface created.");
        resource = wl_resource_create(client, &zwp_input_panel_surface_v1_interface, 1, id);
        wl_resource_set_implementation(resource, &panel_surface_implementation, this, handle_destroy);
        this->surface = surface;
        this->relay   = relay;

        on_surface_commit.set_callback([=] (void*)
        {
            if (wlr_surface_has_buffer(surface) && !surface->mapped)
            {
                wlr_surface_map(surface);
            } else if (!wlr_surface_has_buffer(surface) && surface->mapped)
            {
                wlr_surface_unmap(surface);
            }
        });
        on_surface_commit.connect(&surface->events.commit);
        // Initial commit, maybe already ok?
        on_surface_commit.emit(NULL);

        on_surface_destroy.set_callback([=] (void*)
        {
            if (surface->mapped)
            {
                wlr_surface_unmap(surface);
            }

            on_surface_destroy.disconnect();
            on_surface_commit.disconnect();
        });
        on_surface_destroy.connect(&surface->events.destroy);
    }

    void set_overlay_panel()
    {
        LOGC(IM, "Input method panel surface set to overlay.");
        popup = wf::text_input_v3_popup::create(relay, surface);
        if (surface->mapped)
        {
            popup->map();
        }
    }

    ~wayfire_input_method_v1_panel_surface()
    {
        if (popup && popup->is_mapped())
        {
            popup->unmap();
        }
    }

  private:
    wl_resource *resource;
    wlr_surface *surface;
    wf::text_input_v3_im_relay_interface_t *relay;
    std::shared_ptr<wf::text_input_v3_popup> popup = nullptr;

    wf::wl_listener_wrapper on_surface_commit;
    wf::wl_listener_wrapper on_surface_destroy;

    static void handle_destroy(wl_resource *destroy)
    {
        auto panel = (wayfire_input_method_v1_panel_surface*)wl_resource_get_user_data(destroy);
        delete panel;
    }
};

void handle_input_panel_get_input_panel_surface(wl_client *client, wl_resource *resource,
    uint32_t id, struct wl_resource *surface)
{
    new wayfire_input_method_v1_panel_surface(client, id,
        (wf::text_input_v3_im_relay_interface_t*)wl_resource_get_user_data(resource),
        (wlr_surface*)wl_resource_get_user_data(surface));
}

void handle_input_panel_surface_set_toplevel(wl_client *client, wl_resource *resource,
    wl_resource *output, uint32_t position)
{
    LOGE("The set toplevel request is not supported by the IM-v1 implementation!");
}

void handle_input_panel_surface_set_overlay_panel(wl_client *client, wl_resource *resource)
{
    auto panel = (wayfire_input_method_v1_panel_surface*)wl_resource_get_user_data(resource);
    if (panel)
    {
        panel->set_overlay_panel();
    }
}

class wayfire_input_method_v1 : public wf::plugin_interface_t, public wf::text_input_v3_im_relay_interface_t
{
  public:
    void init() override
    {
        if (enable_input_method_v2)
        {
            LOGE("Enabling both input-method-v2 and input-method-v1 is a bad idea!");
            return;
        }

        input_method_manager = wl_global_create(wf::get_core().display, &zwp_input_method_v1_interface, 1,
            this, handle_bind_im_v1);
        input_panel_manager = wl_global_create(wf::get_core().display, &zwp_input_panel_v1_interface, 1,
            this, handle_bind_im_panel_v1);

        if (enable_text_input_v1)
        {
            text_input_v1_manager = wl_global_create(wf::get_core().display,
                &zwp_text_input_manager_v1_interface, 1, this, handle_bind_text_input_v1);
        }

        if (enable_text_input_v3)
        {
            wf::get_core().protocols.text_input = wlr_text_input_manager_v3_create(wf::get_core().display);
            on_text_input_v3_created.connect(&wf::get_core().protocols.text_input->events.text_input);
            on_text_input_v3_created.set_callback([&] (void *data)
            {
                handle_text_input_v3_created(static_cast<wlr_text_input_v3*>(data));
            });
        }

        wf::get_core().connect(&on_keyboard_focus_changed);
    }

    void fini() override
    {
        if (input_method_manager)
        {
            reset_current_im_context();
            wl_global_destroy(input_method_manager);
            if (current_im)
            {
                wl_resource_set_user_data(current_im, NULL);
            }
        }

        if (text_input_v1_manager)
        {
            wl_global_destroy(text_input_v1_manager);
            for (auto& [input, _] : im_text_inputs_v1)
            {
                wl_resource_set_user_data(input, NULL);
            }
        }
    }

    bool is_unloadable() override
    {
        return false;
    }

    wf::signal::connection_t<wf::keyboard_focus_changed_signal> on_keyboard_focus_changed =
        [=] (wf::keyboard_focus_changed_signal *ev)
    {
        auto view = wf::node_to_view(ev->new_focus);
        auto surf = view ? view->get_wlr_surface() : nullptr;

        if (last_focus_surface != surf)
        {
            reset_current_im_context();
            last_focus_surface = surf;
            for_each_text_input([=] (wayfire_im_text_input_base_t *text_input)
            {
                text_input->set_focus_surface(last_focus_surface);
            });
        }
    };

    // Handlers for text-input-v1

  private:
    wl_global *text_input_v1_manager = nullptr;

    static void handle_bind_text_input_v1(wl_client *client, void *data, uint32_t version, uint32_t id)
    {
        ((wayfire_input_method_v1*)data)->bind_text_input_v1_manager(client, id);
    }

    void bind_text_input_v1_manager(wl_client *client, uint32_t id)
    {
        wl_resource *resource = wl_resource_create(client, &zwp_text_input_manager_v1_interface, 1, id);
        static const struct zwp_text_input_manager_v1_interface text_input_manager_implementation = {
            handle_create_text_input_v1
        };

        wl_resource_set_implementation(resource, &text_input_manager_implementation, this, NULL);
    }

    static void handle_create_text_input_v1(wl_client *client, wl_resource *resource, uint32_t id)
    {
        auto self = (wayfire_input_method_v1*)wl_resource_get_user_data(resource);
        auto ti_resource = wl_resource_create(client, &zwp_text_input_v1_interface, 1, id);

        // Define the struct with function pointers
        static const struct zwp_text_input_v1_interface text_input_v1_impl = {
            .activate   = handle_text_input_v1_activate,
            .deactivate = handle_text_input_v1_deactivate,
            .show_input_panel = handle_text_input_v1_show_input_panel,
            .hide_input_panel = handle_text_input_v1_hide_input_panel,
            .reset = handle_text_input_v1_reset,
            .set_surrounding_text = handle_text_input_v1_set_surrounding_text,
            .set_content_type     = handle_text_input_v1_set_content_type,
            .set_cursor_rectangle = handle_text_input_v1_set_cursor_rectangle,
            .set_preferred_language = handle_text_input_v1_set_preferred_language,
            .commit_state  = handle_text_input_v1_commit_state,
            .invoke_action = handle_text_input_v1_invoke_action
        };

        wl_resource_set_implementation(ti_resource, &text_input_v1_impl, self, handle_text_input_v1_destroy);
        self->im_text_inputs_v1[ti_resource] = std::make_unique<wayfire_im_v1_text_input_v1>(ti_resource);
    }

    static void handle_text_input_v1_destroy(wl_resource *resource)
    {
        auto self = static_cast<wayfire_input_method_v1*>(wl_resource_get_user_data(resource));
        if (self)
        {
            self->im_handle_text_input_disable(self->im_text_inputs_v1[resource].get());
            self->im_text_inputs_v1.erase(resource);
        }
    }

    static void handle_text_input_v1_activate(wl_client *client, wl_resource *resource, wl_resource *seat,
        wl_resource *surface)
    {
        auto self = static_cast<wayfire_input_method_v1*>(wl_resource_get_user_data(resource));
        auto *ti  = self->im_text_inputs_v1[resource].get();

        if (!ti->can_focus() || (ti->current_focus->resource != surface))
        {
            LOGC(IM, "text-input-v1: ignore activate request for wrong focus surface!");
            // Only for focused surfaces!
            return;
        }

        if (self->current_im_context)
        {
            self->im_handle_text_input_disable(self->current_im_context->text_input);
        }

        self->im_handle_text_input_enable(ti);
    }

    static void handle_text_input_v1_deactivate(wl_client *client, wl_resource *resource, wl_resource *seat)
    {
        auto self = static_cast<wayfire_input_method_v1*>(wl_resource_get_user_data(resource));
        auto *ti  = self->im_text_inputs_v1[resource].get();
        self->im_handle_text_input_disable(ti);
    }

    static void handle_text_input_v1_show_input_panel(wl_client *client, wl_resource *resource)
    {}

    static void handle_text_input_v1_hide_input_panel(wl_client *client, wl_resource *resource)
    {}

    static void handle_text_input_v1_reset(wl_client *client, wl_resource *resource)
    {
        auto self = static_cast<wayfire_input_method_v1*>(wl_resource_get_user_data(resource));
        if (self->current_im_context)
        {
            zwp_input_method_context_v1_send_reset(self->current_im_context->context);
        }
    }

    static void handle_text_input_v1_set_surrounding_text(wl_client *client, wl_resource *resource,
        const char *text, uint32_t cursor, uint32_t anchor)
    {
        auto self = static_cast<wayfire_input_method_v1*>(wl_resource_get_user_data(resource));
        if (self->current_im_context)
        {
            zwp_input_method_context_v1_send_surrounding_text(self->current_im_context->context,
                text, cursor, anchor);
        }
    }

    static void handle_text_input_v1_set_content_type(wl_client *client, wl_resource *resource,
        uint32_t hint, uint32_t purpose)
    {
        auto self = static_cast<wayfire_input_method_v1*>(wl_resource_get_user_data(resource));
        if (self->current_im_context)
        {
            zwp_input_method_context_v1_send_content_type(self->current_im_context->context,
                hint, purpose);
        }
    }

    static void handle_text_input_v1_set_cursor_rectangle(wl_client *client, wl_resource *resource,
        int32_t x, int32_t y, int32_t width, int32_t height)
    {
        // ignore, we do not support cursor rectangle or input popups for text-input-v1 yet
    }

    static void handle_text_input_v1_set_preferred_language(wl_client *client, wl_resource *resource,
        const char *language)
    {
        auto self = static_cast<wayfire_input_method_v1*>(wl_resource_get_user_data(resource));
        if (self->current_im_context)
        {
            zwp_input_method_context_v1_send_preferred_language(self->current_im_context->context, language);
        }
    }

    static void handle_text_input_v1_commit_state(wl_client *client, wl_resource *resource, uint32_t serial)
    {
        auto self = static_cast<wayfire_input_method_v1*>(wl_resource_get_user_data(resource));
        if (self->current_im_context)
        {
            zwp_input_method_context_v1_send_commit_state(self->current_im_context->context, serial);
        }
    }

    static void handle_text_input_v1_invoke_action(wl_client *client, wl_resource *resource,
        uint32_t button, uint32_t index)
    {
        auto self = static_cast<wayfire_input_method_v1*>(wl_resource_get_user_data(resource));
        if (self->current_im_context)
        {
            zwp_input_method_context_v1_send_invoke_action(self->current_im_context->context, button, index);
        }
    }

    // Handlers for text-input-v3

  private:
    void handle_text_input_v3_created(wlr_text_input_v3 *input)
    {
        im_text_inputs_v3[input] = std::make_unique<wayfire_im_v1_text_input_v3>(input);

        im_text_inputs_v3[input]->on_enable.set_callback([=] (void *data)
        {
            im_handle_text_input_enable(im_text_inputs_v3[input].get());
        });
        im_text_inputs_v3[input]->on_disable.set_callback([=] (void *data)
        {
            im_handle_text_input_disable(im_text_inputs_v3[input].get());
        });
        im_text_inputs_v3[input]->on_destroy.set_callback([=] (void *data)
        {
            handle_text_input_v3_destroyed(input);
        });
        im_text_inputs_v3[input]->on_commit.set_callback([=] (void *data)
        {
            handle_text_input_v3_commit(input);
        });

        im_text_inputs_v3[input]->set_focus_surface(last_focus_surface);
    }

    void handle_text_input_v3_destroyed(wlr_text_input_v3 *input)
    {
        im_handle_text_input_disable(im_text_inputs_v3[input].get());
        im_text_inputs_v3.erase(input);
    }

    void handle_text_input_v3_commit(wlr_text_input_v3 *input)
    {
        if (current_im_context && (current_im_context->text_input == im_text_inputs_v3[input].get()))
        {
            current_im_context->handle_text_input_v3_commit();

            wf::text_input_commit_signal data;
            data.cursor_rect = input->current.cursor_rectangle;
            emit(&data);
        }
    }

    wlr_text_input_v3 *find_focused_text_input_v3() override
    {
        if (!current_im_context)
        {
            return nullptr;
        }

        auto as_v3 = dynamic_cast<wayfire_im_v1_text_input_v3*>(current_im_context->text_input);
        return as_v3 ? as_v3->text_input_v3 : nullptr;
    }

    // Implementation of input-method-v1

  private:
    void bind_input_method_manager(wl_client *client, uint32_t id)
    {
        wl_resource *resource = wl_resource_create(client, &zwp_input_method_v1_interface, 1, id);
        if (current_im)
        {
            LOGE("Trying to bind to input-method-v1 while another input method is active is not supported!");
            wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT, "Input method already bound");
            return;
        }

        LOGC(IM, "Input method bound");
        wl_resource_set_implementation(resource, NULL, this, handle_destroy_im);
        current_im = resource;

        for (auto& [_, im] : im_text_inputs_v3)
        {
            if (im->is_enabled())
            {
                im_handle_text_input_enable(im.get());
            }
        }
    }

    static void handle_bind_im_v1(wl_client *client, void *data, uint32_t version, uint32_t id)
    {
        ((wayfire_input_method_v1*)data)->bind_input_method_manager(client, id);
    }

    static void handle_destroy_im(wl_resource *resource)
    {
        LOGC(IM, "Input method unbound");
        auto data = wl_resource_get_user_data(resource);
        if (data)
        {
            ((wayfire_input_method_v1*)data)->reset_current_im_context(true);
            ((wayfire_input_method_v1*)data)->current_im = nullptr;
        }
    }

    void im_handle_text_input_enable(wayfire_im_text_input_base_t *text_input)
    {
        wf::input_method_v1_activate_signal data;
        wf::get_core().emit(&data);

        if (!current_im)
        {
            LOGC(IM, "No IM currently connected: ignoring enable request.");
            return;
        }

        if (!last_focus_surface || (text_input->current_focus != last_focus_surface))
        {
            LOGC(IM, "Ignoring enable request for text input ", text_input->dbg_handle, ": stale request");
            return;
        }

        if (current_im_context)
        {
            LOGC(IM, "Text input activated while old context is still around?");
            return;
        }

        LOGC(IM, "Enabling IM context for ", text_input->dbg_handle);
        current_im_context = std::make_unique<wayfire_input_method_v1_context>(
            text_input, current_im, &context_implementation);
    }

    void im_handle_text_input_disable(wayfire_im_text_input_base_t *input)
    {
        wf::input_method_v1_deactivate_signal data;
        wf::get_core().emit(&data);

        if (!current_im_context || (current_im_context->text_input != input))
        {
            return;
        }

        reset_current_im_context();
    }

    void reset_current_im_context(bool im_killed = false)
    {
        if (!current_im_context)
        {
            return;
        }

        LOGC(IM, "Disabling IM context for ", current_im_context->text_input->dbg_handle);
        current_im_context->deactivate(im_killed);
        current_im_context.reset();
    }

    // input-method-panel impl

  private:
    void bind_input_method_panel(wl_client *client, uint32_t id)
    {
        LOGC(IM, "Input method panel interface bound");
        wl_resource *resource = wl_resource_create(client, &zwp_input_panel_v1_interface, 1, id);
        wl_resource_set_implementation(resource, &panel_implementation,
            dynamic_cast<wf::text_input_v3_im_relay_interface_t*>(this), handle_destroy_im_panel);
    }

    static void handle_bind_im_panel_v1(wl_client *client, void *data, uint32_t version, uint32_t id)
    {
        ((wayfire_input_method_v1*)data)->bind_input_method_panel(client, id);
    }

    static void handle_destroy_im_panel(wl_resource *resource)
    {
        LOGC(IM, "Input method panel interface unbound");
    }

  private:
    wf::option_wrapper_t<bool> enable_input_method_v2{"workarounds/enable_input_method_v2"};
    wf::option_wrapper_t<bool> enable_text_input_v1{"input-method-v1/enable_text_input_v1"};
    wf::option_wrapper_t<bool> enable_text_input_v3{"input-method-v1/enable_text_input_v3"};

    wl_global *input_method_manager = NULL;
    wl_global *input_panel_manager  = NULL;

    wl_resource *current_im = NULL;
    wf::wl_listener_wrapper on_text_input_v3_created;
    wlr_surface *last_focus_surface = NULL;

    std::unique_ptr<wayfire_input_method_v1_context> current_im_context = NULL;

    std::map<wl_resource*, std::unique_ptr<wayfire_im_v1_text_input_v1>> im_text_inputs_v1;
    std::map<wlr_text_input_v3*, std::unique_ptr<wayfire_im_v1_text_input_v3>> im_text_inputs_v3;

    void for_each_text_input(std::function<void(wayfire_im_text_input_base_t*)> func)
    {
        for (auto& [_, im] : im_text_inputs_v1)
        {
            func(im.get());
        }

        for (auto& [_, im] : im_text_inputs_v3)
        {
            func(im.get());
        }
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_input_method_v1);

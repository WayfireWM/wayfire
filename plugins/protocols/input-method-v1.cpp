#include <fcntl.h>
#include <unistd.h>
#include <wayfire/plugin.hpp>
#include <wayfire/core.hpp>
#include "input-method-unstable-v1-protocol.h"
#include "wayfire/option-wrapper.hpp"
#include "wayfire/nonstd/wlroots-full.hpp"
#include "wayfire/nonstd/wlroots.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/signal-definitions.hpp"

class wayfire_im_v1_text_input_v3
{
  public:
    wayfire_im_v1_text_input_v3(wlr_text_input_v3 *text_input)
    {
        this->text_input = text_input;
        this->client     = wl_resource_get_client(text_input->resource);

        on_enable.connect(&text_input->events.enable);
        on_disable.connect(&text_input->events.disable);
        on_destroy.connect(&text_input->events.destroy);
        on_commit.connect(&text_input->events.commit);
    }

    void set_focus_surface(wlr_surface *surface)
    {
        wl_client *next_client = surface ? wl_resource_get_client(surface->resource) : nullptr;

        if (current_focus)
        {
            if (!next_client || (next_client != client) || (surface != current_focus))
            {
                LOGC(IM, "Leave text input ti=", text_input);
                wlr_text_input_v3_send_leave(text_input);
                current_focus = nullptr;
            }
        }

        if ((next_client == client) && (surface != current_focus))
        {
            LOGC(IM, "Enter text input ti=", text_input, " surface=", surface);
            wlr_text_input_v3_send_enter(text_input, surface);
            current_focus = surface;
        }
    }

    wlr_text_input_v3 *text_input = NULL;
    wl_client *client = NULL;
    wlr_surface *current_focus = NULL;

    wf::wl_listener_wrapper on_enable;
    wf::wl_listener_wrapper on_disable;
    wf::wl_listener_wrapper on_destroy;
    wf::wl_listener_wrapper on_commit;
};

static void handle_ctx_destruct_final(wl_resource *resource)
{
    LOGI("Destroy!");
}

class wayfire_input_method_v1_context
{
  public:
    wayfire_input_method_v1_context(wlr_text_input_v3 *text_input, wl_resource *current_im,
        const struct zwp_input_method_context_v1_interface *context_impl)
    {
        this->text_input = text_input;
        this->current_im = current_im;

        LOGI("Start ctx");

        context = wl_resource_create(wl_resource_get_client(current_im),
            &zwp_input_method_context_v1_interface, 1, 0);
        wl_resource_set_implementation(context, context_impl, this, handle_ctx_destruct_final);
        zwp_input_method_v1_send_activate(current_im, context);

        LOGI("End ctx ", context);
    }

    void handle_text_input_commit()
    {
        return;
        LOGC(IM, "Commit serial=", ctx_serial);
        zwp_input_method_context_v1_send_content_type(context,
            text_input->current.content_type.hint, text_input->current.content_type.purpose);
        zwp_input_method_context_v1_send_surrounding_text(context,
            text_input->current.surrounding.text ?: "",
            text_input->current.surrounding.cursor,
            text_input->current.surrounding.anchor);

        zwp_input_method_context_v1_send_commit_state(context, ctx_serial++);
    }

    void deactivate()
    {
        return;
        LOGI("Start deactivate");
        wl_resource_set_user_data(context, NULL);
        zwp_input_method_v1_send_deactivate(current_im, context);
        this->text_input = NULL;

        wl_resource_destroy(active_grab_keyboard);
        on_keyboard_key.disconnect();
        LOGI("End deactivate");
    }

    void handle_im_key(uint32_t time, uint32_t key, uint32_t state)
    {
        wlr_seat_keyboard_notify_key(this->text_input->seat, time, key, state);
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
        wlr_seat_keyboard_notify_modifiers(this->text_input->seat, &mods);
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
        self->active_grab_keyboard = NULL;
        self->last_sent_keymap_keyboard = NULL;
    }

    wf::signal::connection_t<wf::input_event_signal<wlr_keyboard_key_event>> on_keyboard_key =
        [=] (wf::input_event_signal<wlr_keyboard_key_event> *ev)
    {
        if (active_grab_keyboard && (ev->mode == wf::input_event_processing_mode_t::FULL))
        {
            check_send_keymap(wlr_keyboard_from_input_device(ev->device));
            ev->mode = wf::input_event_processing_mode_t::NO_CLIENT;
            wl_keyboard_send_key(active_grab_keyboard, vkbd_serial++, ev->event->time_msec,
                ev->event->keycode, ev->event->state);
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
    }

    wlr_keyboard *last_sent_keymap_keyboard = NULL;
    wl_resource *active_grab_keyboard = NULL;

    int32_t cursor = 0;
    uint32_t ctx_serial  = 0;
    uint32_t vkbd_serial = 0;

    wl_resource *current_im = NULL;
    wl_resource *context    = NULL;

    // NULL if inactive
    wlr_text_input_v3 *text_input = NULL;
};

void handle_im_context_destroy(wl_client *client, wl_resource *resource)
{
    LOGI("Destroy!");
    wl_resource_destroy(resource);
}

void handle_im_context_commit_string(wl_client *client, wl_resource *resource,
    uint32_t serial, const char *text)
{
    auto context = static_cast<wayfire_input_method_v1_context*>(wl_resource_get_user_data(resource));
    if (context && context->text_input)
    {
        wlr_text_input_v3_send_commit_string(context->text_input, text);
    }
}

void handle_im_context_preedit_string(wl_client *client, wl_resource *resource,
    uint32_t serial, const char *text, const char *commit)
{
    auto context = static_cast<wayfire_input_method_v1_context*>(wl_resource_get_user_data(resource));
    if (context && context->text_input)
    {
        int begin = std::min((int)strlen(text), context->cursor);
        int end   = std::min((int)strlen(text), context->cursor);
        wlr_text_input_v3_send_preedit_string(context->text_input, text, begin, end);
        wlr_text_input_v3_send_done(context->text_input);
    }
}

void handle_im_context_preedit_styling(wl_client *client, wl_resource *resource,
    uint32_t index, uint32_t length, uint32_t style)
{
    // Nothing to do
}

void handle_im_context_preedit_cursor(wl_client *client, wl_resource *resource,
    int32_t index)
{
    auto context = static_cast<wayfire_input_method_v1_context*>(wl_resource_get_user_data(resource));
    if (context && context->text_input)
    {
        context->cursor = index;
    }
}

void handle_im_context_delete_surrounding_text(wl_client *client, wl_resource *resource,
    int32_t index, uint32_t length)
{
    auto context = static_cast<wayfire_input_method_v1_context*>(wl_resource_get_user_data(resource));
    if (context && context->text_input)
    {
        if ((index > 0) || (index + (int32_t)length < 0))
        {
            // Ignore overflows
            return;
        }

        wlr_text_input_v3_send_delete_surrounding_text(context->text_input, -index, -index + length);
        wlr_text_input_v3_send_done(context->text_input);
    }
}

void handle_im_context_cursor_position(wl_client *client, wl_resource *resource,
    int32_t index, int32_t anchor)
{}

void handle_im_context_modifiers_map(wl_client *client, wl_resource *resource,
    struct wl_array *map)
{}

void handle_im_context_keysym(wl_client *client, wl_resource *resource,
    uint32_t serial, uint32_t time, uint32_t sym, uint32_t state,
    uint32_t modifiers)
{}

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
{}

void handle_im_context_text_direction(wl_client *client, wl_resource *resource,
    uint32_t serial, uint32_t direction)
{}

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

class wayfire_input_method_v1 : public wf::plugin_interface_t
{
  public:
    void init() override
    {
        if (enable_input_method_v2)
        {
            LOGE("Enabling both input-method-v2 and input-method-v1 is a bad idea!");
            return;
        }

        wf::get_core().protocols.text_input = wlr_text_input_manager_v3_create(wf::get_core().display);
        input_method_manager = wl_global_create(wf::get_core().display, &zwp_input_method_v1_interface, 1,
            this, handle_bind_im_v1);

        on_text_input_v3_created.connect(&wf::get_core().protocols.text_input->events.text_input);
        on_text_input_v3_created.set_callback([&] (void *data)
        {
            handle_text_input_v3_created(static_cast<wlr_text_input_v3*>(data));
        });

        wf::get_core().connect(&on_keyboard_focus_changed);
    }

    void fini() override
    {
        if (input_method_manager)
        {
            wl_global_destroy(input_method_manager);
        }
    }

    bool is_unloadable() override
    {
        return false;
    }

    wf::signal::connection_t<wf::keyboard_focus_changed_signal> on_keyboard_focus_changed =
        [=] (wf::keyboard_focus_changed_signal *ev)
    {
        auto surf = wf::node_to_view(ev->new_focus);
        if (surf && surf->get_wlr_surface())
        {
            if (last_focus_surface != surf->get_wlr_surface())
            {
                reset_current_im_context();
                last_focus_surface = surf->get_wlr_surface();
                for (auto& [_, im] : im_text_inputs)
                {
                    im->set_focus_surface(last_focus_surface);
                }
            }
        }
    };

    // Handlers for text-input-v3

  private:
    void handle_text_input_v3_created(wlr_text_input_v3 *input)
    {
        im_text_inputs[input] = std::make_unique<wayfire_im_v1_text_input_v3>(input);

        im_text_inputs[input]->on_enable.set_callback([=] (void *data)
        {
            handle_text_input_v3_enable(input);
        });
        im_text_inputs[input]->on_disable.set_callback([=] (void *data)
        {
            handle_text_input_v3_disable(input);
        });
        im_text_inputs[input]->on_destroy.set_callback([=] (void *data)
        {
            handle_text_input_v3_destroyed(input);
        });
        im_text_inputs[input]->on_commit.set_callback([=] (void *data)
        {
            handle_text_input_v3_commit(input);
        });

        im_text_inputs[input]->set_focus_surface(last_focus_surface);
    }

    void handle_text_input_v3_destroyed(wlr_text_input_v3 *input)
    {
        handle_text_input_v3_disable(input);
        im_text_inputs.erase(input);
    }

    void handle_text_input_v3_commit(wlr_text_input_v3 *input)
    {
        if (current_im_context && (current_im_context->text_input == input))
        {
            current_im_context->handle_text_input_commit();
        }
    }

    void handle_text_input_v3_enable(wlr_text_input_v3 *input)
    {
        if (!current_im)
        {
            LOGC(IM, "No IM currently connected: ignoring enable request.");
            return;
        }

        if (im_text_inputs[input]->current_focus != last_focus_surface)
        {
            LOGC(IM, "Ignoring enable request for text input ", input, ": stale request");
            return;
        }

        if (current_im_context)
        {
            LOGC(IM, "Text input activated while old context is still around?");
            return;
        }

        LOGC(IM, "Enabling IM context for ", input);
        current_im_context = std::make_unique<wayfire_input_method_v1_context>(
            input, current_im, &context_implementation);
    }

    void handle_text_input_v3_disable(wlr_text_input_v3 *input)
    {
        if (!current_im_context || (current_im_context->text_input != input))
        {
            return;
        }

        reset_current_im_context();
    }

    void reset_current_im_context()
    {
        if (!current_im_context)
        {
            return;
        }

        LOGC(IM, "Disabling IM context for ", current_im_context->text_input);
        current_im_context->deactivate();
        current_im_context.reset();
    }

    // Implementation of input-method-v1

  private:
    void bind_client(wl_client *client, uint32_t id)
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
    }

    static void handle_bind_im_v1(wl_client *client, void *data, uint32_t version, uint32_t id)
    {
        ((wayfire_input_method_v1*)data)->bind_client(client, id);
    }

    static void handle_destroy_im(wl_resource *resource)
    {
        LOGC(IM, "Input method unbound");
        auto data = wl_resource_get_user_data(resource);
        ((wayfire_input_method_v1*)data)->current_im = nullptr;
    }

  private:
    wf::option_wrapper_t<bool> enable_input_method_v2{"workarounds/enable_input_method_v2"};
    wl_global *input_method_manager = NULL;
    wl_resource *current_im = NULL;
    wf::wl_listener_wrapper on_text_input_v3_created;
    wlr_surface *last_focus_surface = NULL;

    std::unique_ptr<wayfire_input_method_v1_context> current_im_context = NULL;
    std::map<wlr_text_input_v3*, std::unique_ptr<wayfire_im_v1_text_input_v3>> im_text_inputs;
};

DECLARE_WAYFIRE_PLUGIN(wayfire_input_method_v1);

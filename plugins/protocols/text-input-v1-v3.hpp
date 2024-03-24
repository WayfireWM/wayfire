#pragma once

#include "text-input-unstable-v1-protocol.h"
#include "wayfire/nonstd/wlroots-full.hpp"
#include "wayfire/debug.hpp"

class wayfire_im_text_input_base_t
{
  public:
    wayfire_im_text_input_base_t(wl_client *client, void *dbg_handle) : client(client), dbg_handle(dbg_handle)
    {}

    virtual ~wayfire_im_text_input_base_t() = default;

    void set_focus_surface(wlr_surface *surface)
    {
        wl_client *next_client = surface ? wl_resource_get_client(surface->resource) : nullptr;

        if (current_focus)
        {
            if (!next_client || (next_client != client) || (surface != current_focus))
            {
                LOGC(IM, "Leave text input ti=", dbg_handle);
                disable_focus();
                current_focus = nullptr;
            }
        }

        if ((next_client == client) && (surface != current_focus))
        {
            LOGC(IM, "Enter text input ti=", dbg_handle, " surface=", surface);
            enable_focus(surface);
            current_focus = surface;
        }
    }

    virtual void activate(wlr_surface *surface)
    {}
    virtual void deactivate()
    {}

    virtual void send_commit_string(uint32_t serial, const char *text)
    {}

    virtual void send_preedit_string(uint32_t serial, const char *text, const char *commit)
    {}

    virtual void send_preedit_styling(uint32_t index, uint32_t length, uint32_t style)
    {}

    virtual void send_preedit_cursor(int32_t index)
    {}

    virtual void send_delete_surrounding_text(int32_t index, uint32_t length)
    {}

    virtual void send_cursor_position(int32_t index, int32_t anchor)
    {}

    virtual void send_modifiers_map(wl_array *map)
    {}

    virtual void send_keysym(uint32_t serial, uint32_t time, uint32_t sym, uint32_t state,
        uint32_t modifiers)
    {}

    virtual void send_language(uint32_t serial, const char *language)
    {}

    virtual void send_text_direction(uint32_t serial, uint32_t direction)
    {}

    virtual void enable_focus(wlr_surface *surface) = 0;
    virtual void disable_focus() = 0;

    wl_client *client = NULL;
    wlr_surface *current_focus = NULL;

    void *dbg_handle;
};

class wayfire_im_v1_text_input_v1 : public wayfire_im_text_input_base_t
{
  public:
    wayfire_im_v1_text_input_v1(wl_resource *text_input) :
        wayfire_im_text_input_base_t(wl_resource_get_client(text_input), text_input)
    {
        this->text_input_v1 = text_input;
    }

    void enable_focus(wlr_surface *surface) override
    {
        this->enabled = true;
    }

    void disable_focus() override
    {
        this->enabled = false;
    }

    void activate(wlr_surface *surface) override
    {
        zwp_text_input_v1_send_enter(text_input_v1, surface->resource);
    }

    void deactivate() override
    {
        zwp_text_input_v1_send_leave(text_input_v1);
    }

    void send_commit_string(uint32_t serial, const char *text) override
    {
        zwp_text_input_v1_send_commit_string(text_input_v1, serial, text);
    }

    void send_preedit_string(uint32_t serial, const char *text, const char *commit) override
    {
        zwp_text_input_v1_send_preedit_string(text_input_v1, serial, text, commit);
    }

    void send_preedit_styling(uint32_t index, uint32_t length, uint32_t style) override
    {
        zwp_text_input_v1_send_preedit_styling(text_input_v1, index, length, style);
    }

    void send_preedit_cursor(int32_t index) override
    {
        zwp_text_input_v1_send_preedit_cursor(text_input_v1, index);
    }

    void send_delete_surrounding_text(int32_t index, uint32_t length) override
    {
        zwp_text_input_v1_send_delete_surrounding_text(text_input_v1, index, length);
    }

    void send_cursor_position(int32_t index, int32_t anchor) override
    {
        zwp_text_input_v1_send_cursor_position(text_input_v1, index, anchor);
    }

    void send_modifiers_map(struct wl_array *map) override
    {
        zwp_text_input_v1_send_modifiers_map(text_input_v1, map);
    }

    void send_keysym(uint32_t serial, uint32_t time, uint32_t sym, uint32_t state,
        uint32_t modifiers) override
    {
        zwp_text_input_v1_send_keysym(text_input_v1, serial, time, sym, state, modifiers);
    }

    void send_language(uint32_t serial, const char *language) override
    {
        zwp_text_input_v1_send_language(text_input_v1, serial, language);
    }

    void send_text_direction(uint32_t serial, uint32_t direction) override
    {
        zwp_text_input_v1_send_text_direction(text_input_v1, serial, direction);
    }

    wl_resource *text_input_v1;
    bool enabled = false;

    bool can_focus() const
    {
        return enabled;
    }
};

class wayfire_im_v1_text_input_v3 : public wayfire_im_text_input_base_t
{
  public:
    wayfire_im_v1_text_input_v3(wlr_text_input_v3 *text_input) :
        wayfire_im_text_input_base_t(wl_resource_get_client(text_input->resource), text_input)
    {
        this->text_input_v3 = text_input;
        on_enable.connect(&text_input->events.enable);
        on_disable.connect(&text_input->events.disable);
        on_destroy.connect(&text_input->events.destroy);
        on_commit.connect(&text_input->events.commit);
    }

    void enable_focus(wlr_surface *surface) override
    {
        wlr_text_input_v3_send_enter(text_input_v3, surface);
    }

    void disable_focus() override
    {
        wlr_text_input_v3_send_leave(text_input_v3);
    }

    bool is_enabled() const
    {
        return text_input_v3->current_enabled;
    }

    wlr_text_input_v3 *text_input_v3 = NULL;

    wf::wl_listener_wrapper on_enable;
    wf::wl_listener_wrapper on_disable;
    wf::wl_listener_wrapper on_destroy;
    wf::wl_listener_wrapper on_commit;
    int32_t cursor = 0;

    void send_commit_string(uint32_t serial, const char *text) override
    {
        wlr_text_input_v3_send_commit_string(text_input_v3, text);
        wlr_text_input_v3_send_done(text_input_v3);
    }

    void send_preedit_string(uint32_t serial, const char *text, const char *commit) override
    {
        int begin = std::min((int)strlen(text), cursor);
        int end   = std::min((int)strlen(text), cursor);
        // Send null preedit_string if it's empty.
        //
        // This makes GTK to emit preedit-end signals so that vte paints its
        // cursor. The preedit_string from input-method-v1 can't be null.
        auto preedit_string = strlen(text) > 0 ? text : nullptr;
        wlr_text_input_v3_send_preedit_string(text_input_v3, preedit_string, begin, end);
        wlr_text_input_v3_send_done(text_input_v3);
    }

    void send_preedit_cursor(int32_t index) override
    {
        cursor = index;
    }

    void send_delete_surrounding_text(int32_t index, uint32_t length) override
    {
        if ((index > 0) || (index + (int32_t)length < 0))
        {
            // Ignore overflows
            return;
        }

        wlr_text_input_v3_send_delete_surrounding_text(text_input_v3, -index, -index + length);
        wlr_text_input_v3_send_done(text_input_v3);
    }
};

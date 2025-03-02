#include "wayfire/core.hpp"

#include "wayfire/seat.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/scene-operations.hpp"
#include "wayfire/unstable/wlr-surface-node.hpp"
#include "wayfire/unstable/wlr-surface-controller.hpp"
#include "wayfire/output.hpp"
#include "wayfire/output-layout.hpp"
#include "wayfire/signal-definitions.hpp"

#include <wayfire/plugin.hpp>
#include <wayfire/util.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/plugins/common/simple-text-node.hpp>
#include <wayfire/util/log.hpp>

class lock_surface_keyboard_interaction : public wf::keyboard_interaction_t
{
  public:
    lock_surface_keyboard_interaction(wlr_surface *surface) : surface(surface)
    {}

    void handle_keyboard_enter(wf::seat_t *seat) override
    {
        wlr_seat_keyboard_enter(seat->seat, surface, nullptr, 0, nullptr);
    }

    void handle_keyboard_leave(wf::seat_t *seat) override
    {
        wlr_seat_keyboard_clear_focus(seat->seat);
    }

    void handle_keyboard_key(wf::seat_t *seat, wlr_keyboard_key_event event) override
    {
        wlr_seat_keyboard_notify_key(seat->seat, event.time_msec, event.keycode, event.state);
    }

  private:
    wlr_surface *surface;
};

// Mixin class for common functionality between lock_surface_node and lock_crashed_node.
template<class... Node>
class lock_base_node : public Node...
{
  public:
    template<typename... T>
    lock_base_node(wf::output_t *output, T&&... v) : Node(std::forward<T>(v)...)..., output(output)
    {}

    wf::keyboard_focus_node_t keyboard_refocus(wf::output_t *output)
    {
        if (output != this->output)
        {
            return wf::keyboard_focus_node_t{};
        }

        wf::keyboard_focus_node_t node = {
            .node = this,
            .importance = wf::focus_importance::HIGH,
            .allow_focus_below = false,
        };
        return node;
    }

  protected:
    wf::output_t *output;
};

// Scenegraph node for the surface displayed by the session lock client.
class lock_surface_node : public lock_base_node<wf::scene::wlr_surface_node_t>
{
  public:
    lock_surface_node(wlr_session_lock_surface_v1 *lock_surface, wf::output_t *output) :
        lock_base_node(output, lock_surface->surface, true /* autocommit */),
        lock_surface(lock_surface),
        interaction(std::make_unique<lock_surface_keyboard_interaction>(lock_surface->surface))
    {}

    void configure(wf::dimensions_t size)
    {
        wlr_session_lock_surface_v1_configure(lock_surface, size.width, size.height);
        LOGC(LSHELL, "surface_configure on ", lock_surface->output->name, " ", size);
    }

    void display()
    {
        auto layer_node = output->node_for_layer(wf::scene::layer::LOCK);
        wf::scene::add_front(layer_node, shared_from_this());
        wf::wlr_surface_controller_t::create_controller(lock_surface->surface, layer_node);
        wf::get_core().seat->set_active_node(shared_from_this());
        wf::get_core().seat->refocus();
    }

    void destroy()
    {
        wf::scene::damage_node(shared_from_this(), get_bounding_box());
        wf::wlr_surface_controller_t::try_free_controller(this->lock_surface->surface);
        wf::scene::remove_child(shared_from_this());
        const char *name = this->output->handle ? this->output->handle->name : "(deleted)";
        this->interaction = std::make_unique<wf::keyboard_interaction_t>();
        LOGC(LSHELL, "lock_surface on ", name, " destroyed");
    }

    wf::keyboard_interaction_t& keyboard_interaction()
    {
        return *interaction;
    }

  private:
    wlr_session_lock_surface_v1 *lock_surface;
    std::unique_ptr<wf::keyboard_interaction_t> interaction;
};

// Scenegraph node used to keep the screen locked if the session lock client crashes.
class lock_crashed_node : public lock_base_node<simple_text_node_t>
{
  public:
    lock_crashed_node(wf::output_t *output) : lock_base_node(output)
    {
        set_position({0, 0});
        // TODO: it seems better to create the node and add it to the back of the scenegraph so
        // it will be displayed if the client crashes and its surface is destroyed.
        // Unfortunately this causes the surface to briefly appear before the lock screen.
        // So make the background completely transparent instead, and then add the text
        // when the client suraface is destroyed.
        wf::cairo_text_t::params params(
            1280 /* font_size */,
            wf::color_t{0.1, 0.1, 0.1, 0} /* bg_color */,
            wf::color_t{0.9, 0.9, 0.9, 1} /* fg_color */);
        params.rounded_rect = true;
        set_text_params(params);
        set_size(output->get_screen_size());
    }

    void display(std::string text)
    {
        wf::cairo_text_t::params params(
            1280 /* font_size */,
            wf::color_t{0, 0, 0, 1} /* bg_color */,
            wf::color_t{0.9, 0.9, 0.9, 1} /* fg_color */);
        set_text_params(params);
        // TODO: make the text smaller and display a useful message instead of a big explosion.
        set_text(text);
        auto layer_node = output->node_for_layer(wf::scene::layer::LOCK);
        if (parent() == nullptr)
        {
            wf::scene::add_back(layer_node, shared_from_this());
        }

        wf::get_core().seat->set_active_node(shared_from_this());
    }

    void display_crashed()
    {
        display("💥");
    }

    // Ensure pointer interaction is not passed to views behind this node.
    std::optional<wf::scene::input_node_t> find_node_at(const wf::pointf_t& at) override
    {
        wf::scene::input_node_t result;
        result.node = this;
        result.local_coords = at;
        return result;
    }
};

class wf_session_lock_plugin : public wf::plugin_interface_t
{
    enum lock_state
    {
        LOCKING,
        LOCKED,
        UNLOCKED,
        DESTROYED,
        ZOMBIE,
    };

    struct output_state
    {
        std::shared_ptr<lock_surface_node> surface_node;
        wf::wl_listener_wrapper surface_destroy;
        std::shared_ptr<lock_crashed_node> crashed_node;

        output_state(wf::output_t *output)
        {
            crashed_node = std::make_shared<lock_crashed_node>(output);
            crashed_node->set_text("");
        }

        ~output_state()
        {
            surface_destroy.disconnect();
            surface_node.reset();
            crashed_node.reset();
        }
    };

    class wayfire_session_lock
    {
      public:
        wayfire_session_lock(wf_session_lock_plugin *plugin, wlr_session_lock_v1 *lock) :
            plugin(plugin), lock(lock)
        {
            auto& ol = wf::get_core().output_layout;
            output_added.set_callback([this] (wf::output_added_signal *ev)
            {
                handle_output_added(ev->output);
            });
            ol->connect(&output_added);

            output_removed.set_callback([this] (wf::output_removed_signal *ev)
            {
                handle_output_removed(ev->output);
            });
            ol->connect(&output_removed);

            output_changed.set_callback([this] (wf::output_configuration_changed_signal *ev)
            {
                auto output_state = output_states[ev->output];
                auto size = ev->output->get_screen_size();
                if (output_state->surface_node)
                {
                    output_state->surface_node->configure(size);
                }

                if (output_state->crashed_node)
                {
                    output_state->crashed_node->set_size(size);
                }
            });

            for (auto output : ol->get_outputs())
            {
                handle_output_added(output);
            }

            new_surface.set_callback([this] (void *data)
            {
                wlr_session_lock_surface_v1 *lock_surface = (wlr_session_lock_surface_v1*)data;
                wlr_output *wo = lock_surface->output;

                auto output = wf::get_core().output_layout->find_output(lock_surface->output);
                if (!output || (output_states.find(output) == output_states.end()))
                {
                    LOGE("lock_surface created on deleted output ", wo->name);
                    return;
                }

                auto surface_node = std::make_shared<lock_surface_node>(lock_surface, output);
                surface_node->configure(output->get_screen_size());

                output_states[output]->surface_destroy.set_callback(
                    [this, surface_node, output] (void*)
                {
                    surface_node->destroy();
                    // Output might have been removed.
                    if (output_states.find(output) != output_states.end())
                    {
                        output_states[output]->surface_node.reset();
                        if (output_states[output]->crashed_node)
                        {
                            output_states[output]->crashed_node->display_crashed();
                        }
                    }

                    output_states[output]->surface_destroy.disconnect();
                });
                output_states[output]->surface_destroy.connect(&lock_surface->events.destroy);
                output_states[output]->surface_node = std::move(surface_node);

                if (state == LOCKED)
                {
                    // Output is already inhibited.
                    output_states[output]->surface_node->display();
                } else if (have_all_surfaces())
                {
                    // All lock surfaces ready. Lock.
                    lock_timer.disconnect();
                    lock_all();
                }
            });
            new_surface.connect(&lock->events.new_surface);

            unlock.set_callback([this] (void *data)
            {
                unlock_all();
            });
            unlock.connect(&lock->events.unlock);

            destroy.set_callback([this] (void *data)
            {
                disconnect_signals();
                set_state(state == UNLOCKED ? DESTROYED : ZOMBIE);
                if (state == ZOMBIE)
                {
                    // ensure that the crashed node is displayed in this case as well
                    lock_all();
                }

                LOGC(LSHELL, "session lock destroyed");
            });
            destroy.connect(&lock->events.destroy);

            lock_timer.set_timeout(1000, [this] (void)
            {
                lock_all();
            });

            set_state(LOCKING);
        }

        ~wayfire_session_lock()
        {
            disconnect_signals();
            output_changed.disconnect();
            output_added.disconnect();
            output_removed.disconnect();
            remove_crashed_nodes();
        }

      private:
        void handle_output_added(wf::output_t *output)
        {
            output_states[output] = std::make_shared<output_state>(output);
            if ((state == LOCKED) || (state == ZOMBIE))
            {
                lock_output(output, output_states[output]);
            }

            output->connect(&output_changed);
        }

        void handle_output_removed(wf::output_t *output)
        {
            output->disconnect(&output_changed);
            output_states.erase(output);
        }

        bool have_all_surfaces()
        {
            for (const auto& [_, output_state] : output_states)
            {
                if (!output_state->surface_node)
                {
                    return false;
                }
            }

            return true;
        }

        void lock_output(wf::output_t *output, std::shared_ptr<output_state> output_state)
        {
            output->set_inhibited(true);
            if (state == ZOMBIE)
            {
                output_state->crashed_node->display_crashed();
            } else if (output_state->surface_node)
            {
                output_state->surface_node->display();
            } else
            {
                // if the surface node has not yet been displayed we show an empty surface
                output_state->crashed_node->display("");
            }
        }

        void lock_all()
        {
            for (const auto& [output, output_state] : output_states)
            {
                lock_output(output, output_state);
            }

            if (state != ZOMBIE)
            {
                wlr_session_lock_v1_send_locked(lock);
                set_state(LOCKED);
            }

            LOGC(LSHELL, "lock");
        }

        // TODO: rename to remove_all_surfaces and have it also remove/disconnect lock surfaces?
        void remove_crashed_nodes()
        {
            for (const auto& [output, output_state] : output_states)
            {
                if (output_state->crashed_node)
                {
                    wf::scene::damage_node(output_state->crashed_node,
                        output_state->crashed_node->get_bounding_box());
                    wf::scene::remove_child(output_state->crashed_node);
                    output_state->crashed_node.reset();
                }
            }
        }

        void unlock_all()
        {
            remove_crashed_nodes();
            for (const auto& [output, output_state] : output_states)
            {
                output->set_inhibited(false);
            }

            set_state(UNLOCKED);
            LOGC(LSHELL, "unlock");
        }

        void set_state(lock_state new_state)
        {
            state = new_state;
            plugin->notify_lock_state(state);
        }

        void disconnect_signals()
        {
            // Disconnect lock_session protocol signals and lock timer because the client has gone.
            // Leave output monitoring signals connected: if the client crashed and this lock is
            // in ZOMBIE state, it must keep monitoring output changes to add/remove/resize
            // lock_crashed_nodes.
            new_surface.disconnect();
            unlock.disconnect();
            destroy.disconnect();
            lock_timer.disconnect();
        }

        wf_session_lock_plugin *plugin;
        wlr_session_lock_v1 *lock;
        wf::wl_timer<false> lock_timer;
        std::map<wf::output_t*, std::shared_ptr<output_state>> output_states;

        wf::wl_listener_wrapper new_surface;
        wf::wl_listener_wrapper unlock;
        wf::wl_listener_wrapper destroy;

        wf::signal::connection_t<wf::output_added_signal> output_added;
        wf::signal::connection_t<wf::output_configuration_changed_signal> output_changed;
        wf::signal::connection_t<wf::output_removed_signal> output_removed;

        lock_state state = UNLOCKED;
    };

  public:
    void init() override
    {
        auto display = wf::get_core().display;
        manager = wlr_session_lock_manager_v1_create(display);

        new_lock.set_callback([this] (void *data)
        {
            wlr_session_lock_v1 *wlr_lock = (wlr_session_lock_v1*)data;

            if (!cur_lock)
            {
                cur_lock.reset(new wayfire_session_lock(this, wlr_lock));
                LOGC(LSHELL, "new_lock");
            } else
            {
                LOGE("new_lock: already locked");
                wlr_session_lock_v1_destroy(wlr_lock);
            }
        });
        new_lock.connect(&manager->events.new_lock);

        destroy.set_callback([] (void *data)
        {
            LOGC(LSHELL, "session_lock_manager destroyed");
        });
        destroy.connect(&manager->events.destroy);
    }

    void notify_lock_state(lock_state state)
    {
        switch (state)
        {
          case UNLOCKED:
          case LOCKING:
            break;

          case LOCKED:
            // Screen locked.
            // If a previous lock is in zombie state, delete it, so it stops listening for output
            // changes, removes lock_crashed_nodes, etc.
            prev_lock.reset();
            break;

          case DESTROYED:
            // Session lock client terminated after unlocking.
            cur_lock.reset();
            wf::get_core().seat->refocus();
            break;

          case ZOMBIE:
            // Session lock client crashed.
            // Keep session locked, but remember previous lock so that if a new client connects and
            // then unlocks, crashed nodes are removed, outputs are un-inhibited, etc.
            LOGC(LSHELL, "session_lock_manager destroyed");
            prev_lock = std::move(cur_lock);
            break;
        }
    }

    void fini() override
    {
        // TODO: unlock everything?
    }

    bool is_unloadable() override
    {
        return false;
    }

  private:
    wlr_session_lock_manager_v1 *manager;
    wf::wl_listener_wrapper new_lock;
    wf::wl_listener_wrapper destroy;

    std::shared_ptr<wayfire_session_lock> cur_lock, prev_lock;
};

DECLARE_WAYFIRE_PLUGIN(wf_session_lock_plugin);

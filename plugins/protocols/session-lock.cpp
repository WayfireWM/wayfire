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

enum lock_state
{
    LOCKING,
    LOCKED,
    UNLOCKED,
    DESTROYED,
    ZOMBIE,
} state;

class wf_session_lock_plugin : public wf::plugin_interface_t
{
    class wayfire_session_lock;

    class lock_surface_node : public wf::scene::wlr_surface_node_t
    {
      public:
        lock_surface_node(wayfire_session_lock *lock,
            wlr_session_lock_surface_v1 *lock_surface,
            wf::output_t *output) :
            wf::scene::wlr_surface_node_t(lock_surface->surface, true /* autocommit */),
            lock(lock),
            lock_surface(lock_surface),
            output(output),
            interaction(std::make_unique<lock_surface_keyboard_interaction>(lock_surface->surface,
                lock_surface->output))
        {
            lock_surface_destroy.set_callback([this] (void *data)
            {
                wf::wlr_surface_controller_t::try_free_controller(this->lock_surface->surface);
                wf::scene::remove_child(shared_from_this());
                lock_surface_destroy.disconnect();
                this->lock->surface_destroyed(this->output);
                const char *name = this->output->handle ? this->output->handle->name : "(deleted)";
                this->interaction = std::make_unique<wf::keyboard_interaction_t>();
                LOGC(LSHELL, "lock_surface on ", name, " destroyed");
            });
            lock_surface_destroy.connect(&lock_surface->events.destroy);
        }

        void attach_to_layer()
        {
            auto layer_node = output->node_for_layer(wf::scene::layer::LOCK);
            wf::scene::add_front(layer_node, shared_from_this());
            wf::wlr_surface_controller_t::create_controller(lock_surface->surface, layer_node);
            wf::get_core().seat->set_active_node(shared_from_this());
            wf::get_core().seat->refocus();
        }

        wf::keyboard_focus_node_t keyboard_refocus(wf::output_t *output) override
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

        class lock_surface_keyboard_interaction : public wf::keyboard_interaction_t
        {
          public:
            lock_surface_keyboard_interaction(wlr_surface *surface, wlr_output *output) :
                surface(surface), output(output)
            {}

            void handle_keyboard_enter(wf::seat_t *seat)
            {
                wlr_seat_keyboard_enter(seat->seat, surface, nullptr, 0, nullptr);
            }

            void handle_keyboard_leave(wf::seat_t *seat)
            {
                wlr_seat_keyboard_clear_focus(seat->seat);
            }

            void handle_keyboard_key(wf::seat_t *seat, wlr_keyboard_key_event event)
            {
                wlr_seat_keyboard_notify_key(seat->seat, event.time_msec, event.keycode, event.state);
            }

          private:
            wlr_surface *surface;
            wlr_output *output;
        };

        wf::keyboard_interaction_t& keyboard_interaction()
        {
            return *interaction;
        }

      private:
        wayfire_session_lock *lock;
        wlr_session_lock_surface_v1 *lock_surface;
        wf::output_t *output;
        std::unique_ptr<wf::keyboard_interaction_t> interaction;
        wf::wl_listener_wrapper lock_surface_destroy;
    };

    class backup_node : public simple_text_node_t
    {
      public:
        backup_node(wf::output_t *output) : simple_text_node_t()
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
            params.rounded_rect = false;
            set_text_params(params);
            set_size(output->get_screen_size());
        }

        void display()
        {
            wf::cairo_text_t::params params(
                1280 /* font_size */,
                wf::color_t{0, 0, 0, 1} /* bg_color */,
                wf::color_t{0.9, 0.9, 0.9, 1} /* fg_color */);
            set_text_params(params);
            // TODO: make the text smaller and display a useful message instead of a big explosion.
            set_text("💥");
        }
    };

    struct output_state
    {
        output_state(wf::output_t *output) : output(output)
        {}

        wf::output_t *output;
        std::shared_ptr<lock_surface_node> surface;
        std::shared_ptr<backup_node> backup_surface;
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

                auto size = output->get_screen_size();
                wlr_session_lock_surface_v1_configure(lock_surface, size.width, size.height);
                LOGC(LSHELL, "surface_configure on ", wo->name, " ", size.width, "x", size.height);

                // TODO: hook into output size changes and reconfigure.

                output_states[output]->surface = std::make_shared<lock_surface_node>(
                    this, lock_surface, output);

                if (state == LOCKED)
                {
                    // Output is already inhibited.
                    output_states[output]->surface->attach_to_layer();
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
                output_added.disconnect();
                output_removed.disconnect();
                new_surface.disconnect();
                unlock.disconnect();
                destroy.disconnect();
                set_state(state == UNLOCKED ? DESTROYED : ZOMBIE);
                LOGC(LSHELL, "session lock destroyed");
            });
            destroy.connect(&lock->events.destroy);

            lock_timer.set_timeout(1000, [this] (void)
            {
                lock_all();
            });
        }

        void surface_destroyed(wf::output_t *output)
        {
            if (output_states.find(output) != output_states.end())
            {
                output_states[output]->surface.reset();
                if (output_states[output]->backup_surface)
                {
                    output_states[output]->backup_surface->display();
                }
            }
        }

        ~wayfire_session_lock()
        {
            remove_backup_surfaces();
        }

      private:
        void handle_output_added(wf::output_t *output)
        {
            output_states[output] = std::make_shared<output_state>(output);
            if (state == LOCKED)
            {
                lock_output(output, output_states[output]);
            }
        }

        void handle_output_removed(wf::output_t *output)
        {
            output_states.erase(output);
        }

        bool have_all_surfaces()
        {
            for (const auto& [_, output_state] : output_states)
            {
                if (output_state->surface == nullptr)
                {
                    return false;
                }
            }

            return true;
        }

        void lock_output(wf::output_t *output, std::shared_ptr<output_state> output_state)
        {
            output->set_inhibited(true);
            if (output_state->surface != nullptr)
            {
                output_state->surface->attach_to_layer();
            }

            output_state->backup_surface = std::make_shared<backup_node>(output);
            output_state->backup_surface->set_text("");
            auto layer_node = output->node_for_layer(wf::scene::layer::LOCK);
            wf::scene::add_back(layer_node, output_state->backup_surface);
        }

        void lock_all()
        {
            for (const auto& [output, output_state] : output_states)
            {
                lock_output(output, output_state);
            }

            wlr_session_lock_v1_send_locked(lock);
            set_state(LOCKED);
        }

        void remove_backup_surfaces()
        {
            for (const auto& [output, output_state] : output_states)
            {
                if (output_state->backup_surface)
                {
                    wf::scene::remove_child(output_state->backup_surface);
                    output_state->backup_surface.reset();
                }
            }
        }

        void unlock_all()
        {
            remove_backup_surfaces();
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

        wf_session_lock_plugin *plugin;
        wlr_session_lock_v1 *lock;
        wf::wl_timer<false> lock_timer;
        std::map<wf::output_t*, std::shared_ptr<output_state>> output_states;

        wf::wl_listener_wrapper new_surface;
        wf::wl_listener_wrapper unlock;
        wf::wl_listener_wrapper destroy;

        wf::signal::connection_t<wf::output_added_signal> output_added;
        wf::signal::connection_t<wf::output_removed_signal> output_removed;
    };

  public:
    void init() override
    {
        auto display = wf::get_core().display;
        manager = wlr_session_lock_manager_v1_create(display);

        new_lock.set_callback([this] (void *data)
        {
            wlr_session_lock_v1 *wlr_lock = (wlr_session_lock_v1*)data;

            if (cur_lock.get() == nullptr)
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

        destroy.set_callback([this] (void *data)
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
            prev_lock.reset();
            break;

          case DESTROYED:
            cur_lock.reset();
            break;

          case ZOMBIE:
            prev_lock = std::move(cur_lock);
            break;

          default:
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

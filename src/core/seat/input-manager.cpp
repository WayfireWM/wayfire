#include <cassert>
#include "pointer.hpp"
#include "wayfire/core.hpp"
#include "wayfire/signal-definitions.hpp"
#include "../core-impl.hpp"
#include "keyboard.hpp"
#include "cursor.hpp"
#include "input-manager.hpp"
#include "wayfire/output-layout.hpp"
#include "wayfire/view.hpp"
#include "wayfire/config-backend.hpp"
#include <wayfire/util/log.hpp>
#include <wayfire/debug.hpp>

#include "switch.hpp"
#include "tablet.hpp"
#include "pointing-device.hpp"

static std::unique_ptr<wf::input_device_impl_t> create_wf_device_for_device(
    wlr_input_device *device)
{
    switch (device->type)
    {
      case WLR_INPUT_DEVICE_SWITCH:
        return std::make_unique<wf::switch_device_t>(device);

      case WLR_INPUT_DEVICE_POINTER:
        return std::make_unique<wf::pointing_device_t>(device);

      case WLR_INPUT_DEVICE_TABLET:
        return std::make_unique<wf::tablet_t>(
            wf::get_core_impl().seat->priv->cursor->cursor, device);

      case WLR_INPUT_DEVICE_TABLET_PAD:
        return std::make_unique<wf::tablet_pad_t>(device);

      default:
        return std::make_unique<wf::input_device_impl_t>(device);
    }
}

void wf::input_manager_t::handle_new_input(wlr_input_device *dev)
{
    LOGI("handle new input: ", dev->name,
        ", default mapping: ", dev->name);
    input_devices.push_back(create_wf_device_for_device(dev));

    wf::input_device_added_signal data;
    data.device = nonstd::make_observer(input_devices.back().get());
    wf::get_core().emit(&data);

    configure_input_devices();
}

void wf::input_manager_t::configure_input_device(std::unique_ptr<wf::input_device_impl_t> & device)
{
    auto dev     = device->get_wlr_handle();
    auto cursor  = wf::get_core().get_wlr_cursor();
    auto section =
        wf::get_core().config_backend->get_input_device_section("input-device", dev);

    auto calibration_matrix = section->get_option("calibration")->get_value_str();
    if (!calibration_matrix.empty())
    {
        device->calibrate_touch_device(calibration_matrix);
    }

    auto mapped_output = section->get_option("output")->get_value_str();
    if (mapped_output.empty())
    {
        if (dev->type == WLR_INPUT_DEVICE_POINTER)
        {
            mapped_output = nonull(wlr_pointer_from_input_device(
                dev)->output_name);
        } else if (dev->type == WLR_INPUT_DEVICE_TOUCH)
        {
            mapped_output =
                nonull(wlr_touch_from_input_device(dev)->output_name);
        } else
        {
            mapped_output = nonull(dev->name);
        }
    }

    auto wo = wf::get_core().output_layout->find_output(mapped_output);
    if (wo)
    {
        LOGC(INPUT_DEVICES, "Mapping input ", dev->name, " to output ", wo->to_string(), ".");
        wlr_cursor_map_input_to_output(cursor, dev, wo->handle);
    } else
    {
        LOGC(INPUT_DEVICES, "Mapping input ", dev->name, " to output null.");
        wlr_cursor_map_input_to_output(cursor, dev, nullptr);
    }
}

void wf::input_manager_t::configure_input_devices()
{
    // Might trigger motion events which we want to avoid at other stages
    auto state = wf::get_core().get_current_state();
    if (state != wf::compositor_state_t::RUNNING)
    {
        return;
    }

    for (auto& device : this->input_devices)
    {
        configure_input_device(device);
    }
}

void wf::input_manager_t::handle_input_destroyed(wlr_input_device *dev)
{
    LOGI("remove input: ", dev->name);
    for (auto& device : input_devices)
    {
        if (device->get_wlr_handle() == dev)
        {
            wf::input_device_removed_signal data;
            data.device = {device};
            wf::get_core().emit(&data);
        }
    }

    auto it = std::remove_if(input_devices.begin(), input_devices.end(),
        [=] (const std::unique_ptr<wf::input_device_impl_t>& idev)
    {
        return idev->get_wlr_handle() == dev;
    });
    input_devices.erase(it, input_devices.end());
}

void load_locked_mods_from_config(xkb_mod_mask_t& locked_mods)
{
    wf::option_wrapper_t<bool> numlock_state, capslock_state;
    numlock_state.load_option("input/kb_numlock_default_state");
    capslock_state.load_option("input/kb_capslock_default_state");

    if (numlock_state)
    {
        locked_mods |= wf::KB_MOD_NUM_LOCK;
    }

    if (capslock_state)
    {
        locked_mods |= wf::KB_MOD_CAPS_LOCK;
    }
}

wf::input_manager_t::input_manager_t()
{
    load_locked_mods_from_config(locked_mods);

    input_device_created.set_callback([&] (void *data)
    {
        auto dev = static_cast<wlr_input_device*>(data);
        assert(dev);
        handle_new_input(dev);
    });
    input_device_created.connect(&wf::get_core().backend->events.new_input);

    config_updated = [=] (auto)
    {
        for (auto& dev : input_devices)
        {
            dev->update_options();
        }
    };

    wf::get_core().connect(&config_updated);

    output_added.set_callback([=] (output_added_signal *ev)
    {
        if (exclusive_client != nullptr)
        {
            ev->output->set_inhibited(true);
        }

        configure_input_devices();
    });
    wf::get_core().output_layout->connect(&output_added);
}

void wf::input_manager_t::set_exclusive_focus(wl_client *client)
{
    exclusive_client = client;
    for (auto& wo : wf::get_core().output_layout->get_outputs())
    {
        wo->set_inhibited(client != nullptr);
    }

    /* We no longer have an exclusively focused client, so we should restore
     * focus to the topmost view */
    if (!client)
    {
        wf::get_core().seat->refocus();
    }
}

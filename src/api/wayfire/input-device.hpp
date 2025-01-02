#ifndef WF_INPUT_DEVICE_HPP
#define WF_INPUT_DEVICE_HPP

#include <wayfire/nonstd/wlroots.hpp>

namespace wf
{
class input_device_t
{
  public:
    /**
     * General comment
     * @return The represented wlr_input_device
     */
    wlr_input_device *get_wlr_handle();

    /**
     * @param enabled Whether the compositor should handle input events from
     * the device
     * @return true if the device state was successfully changed
     */
    bool set_enabled(bool enabled = true);

    /**
     * Calibrate a touch device with a matrix. This function does nothing
     * if called with a device that is not a touch device.
     */
    void calibrate_touch_device(std::string const & cal);

    /**
     * @return true if the compositor should receive events from the device
     */
    bool is_enabled();
    virtual ~input_device_t() = default;

  protected:
    wlr_input_device *handle;
    input_device_t(wlr_input_device *handle);
};
}

#endif /* end of include guard: WF_INPUT_DEVICE_HPP */

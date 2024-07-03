#ifndef OUTPUT_LAYOUT_HPP
#define OUTPUT_LAYOUT_HPP

#include <map>
#include <vector>
#include <memory>
#include <functional>

#include <wayfire/config/types.hpp>
#include <wayfire/nonstd/wlroots.hpp>
#include "geometry.hpp"
#include "wayfire/object.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/util.hpp"

#define RENDER_BIT_DEPTH_DEFAULT 8

namespace wf
{
class output_t;

/* ----------------------------------------------------------------------------/
 * Output signals
 * -------------------------------------------------------------------------- */

/** Base class for all output signals. */

/**
 * on: output-layout
 * when: Each time a new output is added.
 */
struct output_added_signal
{
    wf::output_t *output;
};

/**
 * on: output, output-layout(output-)
 * when: Emitted just before starting the destruction procedure for an output.
 */
struct output_pre_remove_signal
{
    wf::output_t *output;
};

/**
 * on: output-layout
 * when: Each time a new output is added.
 */
struct output_removed_signal
{
    wf::output_t *output;
};

enum output_config_field_t
{
    /** Output source changed */
    OUTPUT_SOURCE_CHANGE    = (1 << 0),
    /** Output mode changed */
    OUTPUT_MODE_CHANGE      = (1 << 1),
    /** Output scale changed */
    OUTPUT_SCALE_CHANGE     = (1 << 2),
    /** Output transform changed */
    OUTPUT_TRANSFORM_CHANGE = (1 << 3),
    /** Output position changed */
    OUTPUT_POSITION_CHANGE  = (1 << 4),
};

struct output_state_t;

/**
 * on: output-layout
 * when: Each time the configuration of the output layout changes.
 */
struct output_layout_configuration_changed_signal
{};

/**
 * on: output
 * when: Each time the output's source, mode, scale, transform and/or position changes.
 */
struct output_configuration_changed_signal
{
    wf::output_t *output;
    output_configuration_changed_signal(const wf::output_state_t& st) : state(st)
    {}

    /**
     * Which output attributes actually changed.
     * A bitwise OR of output_config_field_t.
     */
    uint32_t changed_fields;

    /**
     * The new state of the output.
     */
    const wf::output_state_t& state;
};

/** Represents the source of pixels for this output */
enum output_image_source_t
{
    OUTPUT_IMAGE_SOURCE_INVALID = 0x0,
    /** Output renders itself */
    OUTPUT_IMAGE_SOURCE_SELF    = 0x1,
    /** Output is turned off */
    OUTPUT_IMAGE_SOURCE_NONE    = 0x2,
    /** Output is in DPMS state */
    OUTPUT_IMAGE_SOURCE_DPMS    = 0x3,
    /** Output is in mirroring state */
    OUTPUT_IMAGE_SOURCE_MIRROR  = 0x4,
};

/** Represents the current state of an output as the output layout sees it */
struct output_state_t
{
    /* The current source of the output.
     *
     * If source is none, then the values below don't have a meaning.
     * If source is mirror, then only mirror_from and mode have a meaning */
    output_image_source_t source = OUTPUT_IMAGE_SOURCE_INVALID;

    /** Position for the output */
    wf::output_config::position_t position;

    /** Only width, height and refresh fields are used. */
    wlr_output_mode mode;

    /** Whether a custom mode was requested for the output. */
    bool uses_custom_mode = false;

    /* The transform of the output */
    wl_output_transform transform = WL_OUTPUT_TRANSFORM_NORMAL;
    /* The scale of the output */
    double scale = 1.0;
    /* Whether or not adaptive sync is enabled */
    bool vrr = false;
    /* Output format bit depth */
    int depth = RENDER_BIT_DEPTH_DEFAULT;

    /* Output to take the image from. Valid only if source is mirror */
    std::string mirror_from;

    bool operator ==(const output_state_t& other) const;
};

/** An output configuration is simply a list of each output with its state */
using output_configuration_t = std::map<wlr_output*, output_state_t>;

/* output_layout_t is responsible for managing outputs and their attributes -
 * mode, scale, position, transform. */
class output_layout_t : public wf::signal::provider_t
{
  public:
    output_layout_t(wlr_backend *backend);
    ~output_layout_t();

    /**
     * @return the underlying wlr_output_layout
     */
    wlr_output_layout *get_handle();

    /**
     * @return the output at the given coordinates, or null if no such output
     */
    wf::output_t *get_output_at(int x, int y);

    /**
     * Get the output closest to the given origin and the closest coordinates
     * to origin which lie inside the output.
     *
     * @param origin The start coordinates
     * @param closest The closest point to origin inside the returned output
     * @return the output at the given coordinates
     */
    wf::output_t *get_output_coords_at(wf::pointf_t origin, wf::pointf_t& closest);

    /**
     * @return the number of the active outputs in the output layout
     */
    size_t get_num_outputs();

    /**
     * @return a list of the active outputs in the output layout
     */
    std::vector<wf::output_t*> get_outputs();

    /**
     * @return the "next" output in the layout. It is guaranteed that starting
     * with any output in the layout, and successively calling this function
     * will iterate over all outputs
     */
    wf::output_t *get_next_output(wf::output_t *output);

    /**
     * @return the output_t associated with the wlr_output, or null if the
     * output isn't found
     */
    wf::output_t *find_output(wlr_output *output);
    wf::output_t *find_output(std::string name);

    /**
     * @return the current output configuration. This contains ALL outputs,
     * not just the ones in the actual layout (so disabled ones are included
     * as well)
     */
    output_configuration_t get_current_configuration();

    /**
     * Apply the given configuration. It must contain exactly the outputs
     * returned by get_current_configuration() - no more and no less.
     *
     * Failure to apply the configuration on any output will reset all
     * outputs to their previous state.
     *
     * @param configuration The output configuration to be applied
     * @param test_only     If true, this will only simulate applying
     * the configuration, without actually changing anything
     *
     * @return true on successful application, false otherwise
     */
    bool apply_configuration(const output_configuration_t& configuration,
        bool test_only = false);

    class impl;
    std::unique_ptr<impl> pimpl;
};
}

#endif /* end of include guard: OUTPUT_LAYOUT_HPP */

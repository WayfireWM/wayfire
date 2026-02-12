#pragma once

#include <wayfire/config/option-types.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <vector>

namespace wf
{
namespace option_type
{
/**
 * To create an option of a given type, from_string must be specialized for
 * parsing the type.
 *
 * @param string The string representation of the value.
 * @return The parsed value, if the string was valid.
 */
template<class Type>
std::optional<Type> from_string(
    const std::string& string);

/**
 * To create an option of a given type, to_string must be specialized for
 * converting the type to string.
 * @return The string representation of a value.
 *   It is expected that from_string(to_string(value)) == value.
 */
template<class Type>
std::string to_string(const Type& value);

/**
 * Parse the given string as a signed 32-bit integer in decimal system.
 */
template<>
std::optional<int> from_string<int>(const std::string&);

/**
 * Parse the given string as a boolean value.
 * Truthy values are "True" (any capitalization) and 1.
 * False values are "False" (any capitalization) and 0.
 */
template<>
std::optional<bool> from_string<bool>(const std::string&);

/**
 * Parse the given string as a signed 64-bit floating point number.
 */
template<>
std::optional<double> from_string<double>(const std::string&);

/**
 * Parse the string as a string.
 * The string should not contain newline characters.
 */
template<>
std::optional<std::string> from_string<std::string>(const std::string&);

/**
 * Convert the given bool to a string.
 */
template<>
std::string to_string<bool>(const bool& value);

/**
 * Convert the given integer to a string.
 */
template<>
std::string to_string<int>(const int& value);

/**
 * Convert the given double to a string.
 */
template<>
std::string to_string<double>(const double& value);

/**
 * Convert the given string to a string.
 */
template<>
std::string to_string<std::string>(const std::string& value);
}

/**
 * Represents a color in RGBA format.
 */
struct color_t
{
  public:
    /** Initialize a black transparent color (default) */
    color_t();

    /**
     * Initialize a new color value with the given values
     * Values will be clamped to the [0, 1] range.
     */
    color_t(double r, double g, double b, double a);

    /**
     * Initialize a new color value with the given values.
     * Values will be clamped to the [0, 1] range.
     */
    explicit color_t(const glm::vec4& value);

    /**
     * Compare colors channel-for-channel.
     * Comparisons use a small epsilon 1e-6.
     */
    bool operator ==(const color_t& other) const;

    /** Red channel value */
    double r;
    /** Green channel value */
    double g;
    /** Blue channel value */
    double b;
    /** Alpha channel value */
    double a;
};

namespace option_type
{
/**
 * Create a new color value from the given hex string, format is either
 * #RRGGBBAA or #RGBA.
 */
template<>
std::optional<color_t> from_string(const std::string& value);

/** Convert the color to its hex string representation. */
template<>
std::string to_string(const color_t& value);
}

/**
 * A list of valid modifiers.
 * The enumerations values are the same as the ones in wlroots.
 */
enum keyboard_modifier_t
{
    /* Shift modifier, <shift> */
    KEYBOARD_MODIFIER_SHIFT = 1,
    /* Control modifier, <ctrl> */
    KEYBOARD_MODIFIER_CTRL  = 4,
    /* Alt modifier, <alt> */
    KEYBOARD_MODIFIER_ALT   = 8,
    /* Windows/Mac logo modifier, <super> */
    KEYBOARD_MODIFIER_LOGO  = 64,
};

/**
 * Represents a single keyboard shortcut.
 */
struct keybinding_t
{
  public:
    /**
     * Construct a new keybinding with the given modifier and key.
     */
    keybinding_t(uint32_t modifier, uint32_t keyval);

    /* Check whether two keybindings refer to the same shortcut */
    bool operator ==(const keybinding_t& other) const;

    /** @return The modifiers of the keybinding */
    uint32_t get_modifiers() const;
    /** @return The key of the keybinding */
    uint32_t get_key() const;

  private:
    /** The modifier mask of this keybinding */
    uint32_t mod;
    /** The key of this keybinding */
    uint32_t keyval;
};

namespace option_type
{
/**
 * Construct a new keybinding from the given string description.
 * Format is <modifier1> .. <modifierN> KEY_<keyname>, where whitespace
 * characters between the different modifiers and KEY_* are ignored.
 *
 * For a list of available modifieres, see @keyboard_modifier_t.
 *
 * The KEY_<keyname> is derived from evdev, and possible names are
 * enumerated in linux/input-event-codes.h
 *
 * For example, "<super> <alt> KEY_E" represents pressing the Logo, Alt and
 * E keys together.
 *
 * Special cases are "none" and "disabled", which result in modifiers and
 * key 0.
 */
template<>
std::optional<keybinding_t> from_string(
    const std::string& description);

/** Represent the keybinding as a string. */
template<>
std::string to_string(const keybinding_t& value);
}

/**
 * Represents a single button shortcut (pressing a mouse button while holding
 * modifiers).
 */
struct buttonbinding_t
{
  public:
    /**
     * Construct a new buttonbinding with the given modifier and button.
     */
    buttonbinding_t(uint32_t modifier, uint32_t button);

    /* Check whether two keybindings refer to the same shortcut */
    bool operator ==(const buttonbinding_t& other) const;

    /** @return The modifiers of the buttonbinding */
    uint32_t get_modifiers() const;
    /** @return The button of the buttonbinding */
    uint32_t get_button() const;

  private:
    /** The modifier mask of this keybinding */
    uint32_t mod;
    /** The key of this keybinding */
    uint32_t button;
};

namespace option_type
{
/**
 * Construct a new buttonbinding from the given description.
 * The format is the same as a keybinding, however instead of KEY_* values,
 * the buttons are prefixed with BTN_*
 *
 * Special case are descriptions "none" and "disable", which result in
 * mod = button = 0
 */
template<>
std::optional<buttonbinding_t> from_string(
    const std::string& description);

/** Represent the buttonbinding as a string. */
template<>
std::string to_string(const buttonbinding_t& value);
}

/**
 * The different types of available gestures.
 */
enum touch_gesture_type_t
{
    /* Invalid gesture */
    GESTURE_TYPE_NONE       = 0,
    /* Swipe gesture, i.e moving in one direction */
    GESTURE_TYPE_SWIPE      = 1,
    /* Edge swipe, which is a swipe originating from the edge of the screen */
    GESTURE_TYPE_EDGE_SWIPE = 2,
    /* Pinch gesture, multiple touch points coming closer or farther apart
     * from the center */
    GESTURE_TYPE_PINCH      = 3,
};

enum touch_gesture_direction_t
{
    /* Swipe-specific */
    GESTURE_DIRECTION_LEFT  = (1 << 0),
    GESTURE_DIRECTION_RIGHT = (1 << 1),
    GESTURE_DIRECTION_UP    = (1 << 2),
    GESTURE_DIRECTION_DOWN  = (1 << 3),
    /* Pinch-specific */
    GESTURE_DIRECTION_IN    = (1 << 4),
    GESTURE_DIRECTION_OUT   = (1 << 5),
};

/**
 * Represents a touch gesture.
 *
 * A touch gesture has a type, direction and finger count.
 * Finger count can be arbitrary, although Wayfire supports only gestures
 * with finger count >= 3 currently.
 *
 * Direction can be either one of of @touch_gesture_direction_t or, in case of
 * the swipe gestures, it can be a bitwise OR of two non-opposing directions.
 */
struct touchgesture_t
{
    /**
     * Construct a new touchgesture_t with the given type, direction and finger
     * count. Invalid combinations result in an invalid gesture with type NONE.
     */
    touchgesture_t(touch_gesture_type_t type, uint32_t direction,
        int finger_count);

    /** @return The type of the gesture */
    touch_gesture_type_t get_type() const;

    /** @return The finger count of the gesture, if valid. Undefined otherwise */
    int get_finger_count() const;

    /** @return The direction of the gesture, if valid. Undefined otherwise */
    uint32_t get_direction() const;

    /**
     * Check whether two bindings are equal.
     * Beware that a binding might be only partially set, i.e it might not have
     * a direction. In this case, the direction acts as a wildcard, so the
     * touchgesture_t matches any touchgesture_t of the same type with the same
     * finger count
     */
    bool operator ==(const touchgesture_t& other) const;

  private:
    /** Type of the gesture */
    touch_gesture_type_t type;
    /** Direction of the gesture */
    uint32_t direction;
    /** Number of fingers of the gesture */
    int finger_count;
};

namespace option_type
{
/**
 * Construct a new touchgesture_t with the type, direction and finger count
 * indicated in the description.
 *
 * Format:
 * 1. pinch [in|out] <fingercount>
 * 2. [edge-]swipe up|down|left|right <fingercount>
 * 3. [edge-]swipe up-left|right-down|... <fingercount>
 * 4. disable | none
 */
template<>
std::optional<touchgesture_t> from_string(
    const std::string& description);

/** Represent the touch gesture as a string. */
template<>
std::string to_string(const touchgesture_t& value);
}

/**
 * The available edges of an output.
 */
enum output_edge_t
{
    OUTPUT_EDGE_LEFT   = (1 << 0),
    OUTPUT_EDGE_RIGHT  = (1 << 1),
    OUTPUT_EDGE_TOP    = (1 << 2),
    OUTPUT_EDGE_BOTTOM = (1 << 3),
};

/**
 * Represents a binding which can be activated by moving the mouse into a
 * corner of the screen.
 */
struct hotspot_binding_t
{
    /**
     * Initialize a hotspot with the given edges.
     *
     * @param edges The edges of the hotspot, a bitmask of output_edge_t
     * @param along_edge The size of the hotspot alongside the edge(s)
     *   it is located on.
     * @param across_edge The size of the hotspot away from the edge(s)
     *   it is located on.
     * @param timeout The time in milliseconds needed for the mouse to stay
     *   in the hotspot to activate it.
     */
    hotspot_binding_t(uint32_t edges = 0, int32_t along_edge = 0,
        int32_t away_from_edge = 0, int32_t timeout = 0);

    bool operator ==(const hotspot_binding_t& other) const;

    /** @return The edges this hotspot binding is on. */
    uint32_t get_edges() const;

    /** @return The size along edges. */
    int32_t get_size_along_edge() const;

    /** @return The size away from edges. */
    int32_t get_size_away_from_edge() const;

    /** @return The timeout of the hotspot. */
    int32_t get_timeout() const;

  private:
    uint32_t edges;
    int32_t along;
    int32_t away;
    int32_t timeout;
};

namespace option_type
{
/**
 * Construct a new hotspot_binding_t with the specified edges and size
 *
 * Format:
 * hotspot top|...|top-left|... <along>x<away> <timeout>
 */
template<>
std::optional<hotspot_binding_t> from_string(
    const std::string& description);

/** Represent the hotspot binding as a string. */
template<>
std::string to_string(const hotspot_binding_t& value);
}

/**
 * Represents a binding which can be activated via multiple actions -
 * keybindings, buttonbindings, touch gestures and hotspots.
 */
struct activatorbinding_t
{
  public:
    /**
     * Initialize an empty activator binding, i.e one which cannot be activated
     * in any way.
     */
    activatorbinding_t();
    ~activatorbinding_t();

    /* Copy constructor */
    activatorbinding_t(const activatorbinding_t& other);
    /* Copy assignment */
    activatorbinding_t& operator =(const activatorbinding_t& other);

    /** @return true if the activator is activated by the given keybinding. */
    bool has_match(const keybinding_t& key) const;

    /** @return true if the activator is activated by the given buttonbinding. */
    bool has_match(const buttonbinding_t& button) const;

    /** @return true if the activator is activated by the given gesture. */
    bool has_match(const touchgesture_t& gesture) const;

    /**
     * @return A list of all hotspots which activate this binding.
     */
    const std::vector<wf::hotspot_binding_t>& get_hotspots() const;

    /**
     * @return A list of all unknown bindings which activate this binding.
     */
    const std::vector<std::string>& get_extensions() const;

    /**
     * Check equality of two activator bindings.
     *
     * @return true if the two activator bindings are activated by the exact
     *  same bindings, false otherwise.
     */
    bool operator ==(const activatorbinding_t& other) const;

  public:
    struct impl;
    std::unique_ptr<impl> priv;
};

namespace option_type
{
/**
 * Create an activator string from the given string description.
 * The string consists of valid descriptions of keybindings, buttonbindings
 * and touch gestures, separated by a single '|' sign.
 */
template<>
std::optional<activatorbinding_t> from_string(
    const std::string& string);

/** Represent the activator binding as a string. */
template<>
std::string to_string(const activatorbinding_t& value);
}

/**
 * Types which are related to various output options.
 */
namespace output_config
{
enum mode_type_t
{
    /** Output was configured in automatic mode. */
    MODE_AUTO,
    /** Output was configured with the biggest refresh rate. */
    MODE_HIGHRR,
    /** Output was configured with the biggest resolution. */
    MODE_HIGHRES,
    /** Output was configured to be turned off. */
    MODE_OFF,
    /** Output was configured with a given resolution. */
    MODE_RESOLUTION,
    /** Output was configured to be a mirror of another output. */
    MODE_MIRROR,
};

/**
 * Represents the output mode.
 * It contains different values depending on the source.
 */
struct mode_t
{
    /**
     * Initialize an OFF or AUTO mode.
     *
     * @param auto_on If true, the created mode will be an AUTO mode.
     */
    mode_t(bool auto_on = false);

    /**
     * Initialize an alternative AUTO mode.
     *
     * @param auto_on Must be true in order to choose an alternative mode. Otherwise always OFF mode.
     * @param prioritize_res If true, the created mode will be an HIGHRES mode. Otherwise, HIGHRR.
     */
    mode_t(int prioritize_res = 3);

    /**
     * Initialize the mode with source self.
     *
     * @param width The configured width.
     * @param height The configured height.
     * @param refresh The configured refresh rate, or 0 if undefined.
     */
    mode_t(int32_t width, int32_t height, int32_t refresh);

    /**
     * Initialize a mirror mode.
     */
    mode_t(const std::string& mirror_from);

    /** @return The type of this mode. */
    mode_type_t get_type() const;

    /** @return The configured width, if applicable. */
    int32_t get_width() const;
    /** @return The configured height, if applicable. */
    int32_t get_height() const;
    /** @return The configured refresh rate, if applicable. */
    int32_t get_refresh() const;

    /** @return The configured mirror from output, if applicable. */
    std::string get_mirror_from() const;

    /**
     * Check equality of two modes.
     *
     * @return true if the modes have the same source types and parameters.
     */
    bool operator ==(const mode_t& other) const;

  private:
    int32_t width;
    int32_t height;
    int32_t refresh;

    std::string mirror_from;

    mode_type_t type;
};

/**
 * Represents the output's position.
 */
struct position_t
{
    /** Automatically positioned output. */
    position_t();

    /** Output positioned at a fixed position. */
    position_t(int32_t x, int32_t y);

    /** @return The configured X coordinate. */
    int32_t get_x() const;
    /** @return The configured X coordinate. */
    int32_t get_y() const;

    /** @return whether the output is automatically positioned. */
    bool is_automatic_position() const;

    bool operator ==(const position_t& other) const;

  private:
    int32_t x;
    int32_t y;
    bool automatic;
};
}

namespace option_type
{
/**
 * Create a mode from its string description.
 * The supported formats are:
 *
 * For MODE_AUTO: auto|default
 * For MODE_OFF: off
 * For MODE_RESOLUTION: WxH[@RR]
 * For MODE_MIRROR: mirror <output>
 */
template<>
std::optional<output_config::mode_t> from_string(
    const std::string& string);

/** Represent the activator binding as a string. */
template<>
std::string to_string(const output_config::mode_t& value);

/**
 * Create an output position from its string description.
 * The supported formats are:
 *
 * auto|default
 * x , y
 */
template<>
std::optional<output_config::position_t> from_string(
    const std::string& string);

/** Represent the activator binding as a string. */
template<>
std::string to_string(const output_config::position_t& value);
}
}

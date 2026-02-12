#include <wayfire/config/types.hpp>
#include <vector>
#include <map>
#include <cmath>
#include <algorithm>

#include <libevdev/libevdev.h>
#include <sstream>

/* --------------------------- Primitive types ------------------------------ */
template<>
std::optional<bool> wf::option_type::from_string(const std::string& value)
{
    std::string lowercase = value;
    for (auto& c : lowercase)
    {
        c = std::tolower(c);
    }

    if ((lowercase == "true") || (lowercase == "1"))
    {
        return true;
    }

    if ((lowercase == "false") || (lowercase == "0"))
    {
        return false;
    }

    return {};
}

template<>
std::optional<int> wf::option_type::from_string(const std::string& value)
{
    std::istringstream in{value};
    in.imbue(std::locale::classic());
    int result;
    in >> result;

    if (value != wf::option_type::to_string(result))
    {
        return {};
    }

    return result;
}

/** Attempt to parse a string as an double value */
template<>
std::optional<double> wf::option_type::from_string(const std::string& value)
{
    std::istringstream in{value};
    in.imbue(std::locale::classic());
    double result;
    in >> result;

    if (!in.eof() || in.fail() || value.empty())
    {
        /* XXX: is the check above enough??? Overflow? Underflow? */
        return {};
    }

    return result;
}

template<>
std::optional<std::string> wf::option_type::from_string(const std::string& value)
{
    return value;
}

template<>
std::string wf::option_type::to_string(
    const bool& value)
{
    return value ? "true" : "false";
}

template<>
std::string wf::option_type::to_string(
    const int& value)
{
    std::ostringstream s;
    s.imbue(std::locale::classic());
    s << value;
    return s.str();
}

template<>
std::string wf::option_type::to_string(
    const double& value)
{
    std::ostringstream s;
    s.imbue(std::locale::classic());
    s << std::fixed << value;
    return s.str();
}

template<>
std::string wf::option_type::to_string(
    const std::string& value)
{
    return value;
}

/* ----------------------------- wf::color_t -------------------------------- */
wf::color_t::color_t() :
    color_t(0.0, 0.0, 0.0, 0.0)
{}

wf::color_t::color_t(double r, double g, double b, double a)
{
    this->r = r;
    this->g = g;
    this->b = b;
    this->a = a;
}

wf::color_t::color_t(const glm::vec4& value) :
    color_t(value.r, value.g, value.b, value.a)
{}

static double hex_to_double(std::string value)
{
    char *dummy;
    return std::strtol(value.c_str(), &dummy, 16);
}

static std::optional<wf::color_t> try_parse_rgba(const std::string& value)
{
    wf::color_t parsed = {0, 0, 0, 0};
    std::stringstream ss(value);
    ss.imbue(std::locale::classic());

    bool valid_color =
        (bool)(ss >> parsed.r >> parsed.g >> parsed.b >> parsed.a);

    /* Check nothing else after that */
    std::string dummy;
    valid_color &= !(bool)(ss >> dummy);

    return valid_color ? parsed : std::optional<wf::color_t>{};
}

#include <iostream>
static const std::string hex_digits = "0123456789ABCDEF";
template<>
std::optional<wf::color_t> wf::option_type::from_string(
    const std::string& param_value)
{
    auto value = param_value;
    for (auto& ch : value)
    {
        ch = std::toupper(ch);
    }

    auto as_rgba = try_parse_rgba(value);
    if (as_rgba)
    {
        return as_rgba;
    }

    /* Either #RGBA or #RRGGBBAA */
    if ((value.size() != 5) && (value.size() != 9))
    {
        return {};
    }

    if (value[0] != '#')
    {
        return {};
    }

    if (value.find_first_not_of(hex_digits, 1) != std::string::npos)
    {
        return {};
    }

    double r, g, b, a;

    /* #RRGGBBAA case */
    if (value.size() == 9)
    {
        r = hex_to_double(value.substr(1, 2)) / 255.0;
        g = hex_to_double(value.substr(3, 2)) / 255.0;
        b = hex_to_double(value.substr(5, 2)) / 255.0;
        a = hex_to_double(value.substr(7, 2)) / 255.0;
    } else
    {
        assert(value.size() == 5);
        r = hex_to_double(value.substr(1, 1)) / 15.0;
        g = hex_to_double(value.substr(2, 1)) / 15.0;
        b = hex_to_double(value.substr(3, 1)) / 15.0;
        a = hex_to_double(value.substr(4, 1)) / 15.0;
    }

    return wf::color_t{r, g, b, a};
}

template<>
std::string wf::option_type::to_string(const color_t& value)
{
    const int max_byte = 255;
    const int min_byte = 0;

    auto to_hex = [=] (double number_d)
    {
        int number = std::round(number_d);
        /* Clamp */
        number = std::min(number, max_byte);
        number = std::max(number, min_byte);

        std::string result;
        result += hex_digits[number / 16];
        result += hex_digits[number % 16];
        return result;
    };

    return "#" + to_hex(value.r * max_byte) + to_hex(value.g * max_byte) +
           to_hex(value.b * max_byte) + to_hex(value.a * max_byte);
}

bool wf::color_t::operator ==(const color_t& other) const
{
    constexpr double epsilon = 1e-6;

    bool equal = true;
    equal &= std::abs(this->r - other.r) < epsilon;
    equal &= std::abs(this->g - other.g) < epsilon;
    equal &= std::abs(this->b - other.b) < epsilon;
    equal &= std::abs(this->a - other.a) < epsilon;

    return equal;
}

/* ------------------------- wf::keybinding_t ------------------------------- */
struct general_binding_t
{
    bool enabled;
    uint32_t mods;
    uint32_t value;
};

/**
 * Split @value at non-empty tokens when encountering any of the characters
 * in @at
 */
static std::vector<std::string> split_at(std::string value, std::string at,
    bool allow_empty = false)
{
    /* Trick: add a delimiter at position 0 and at the end
     * to avoid special casing */
    value = at[0] + value + at[0];

    size_t current = 0;
    std::vector<size_t> split_positions = {0};
    while (current < value.size() - 1)
    {
        size_t next_split = value.find_first_of(at, current + 1);
        split_positions.push_back(next_split);
        current = next_split;
    }

    assert(split_positions.size() >= 2);
    std::vector<std::string> tokens;
    for (size_t i = 1; i < split_positions.size(); i++)
    {
        if ((split_positions[i] == split_positions[i - 1] + 1) && !allow_empty)
        {
            continue; // skip empty tokens
        }

        tokens.push_back(value.substr(split_positions[i - 1] + 1,
            split_positions[i] - split_positions[i - 1] - 1));
    }

    return tokens;
}

static std::map<std::string, wf::keyboard_modifier_t> modifier_names =
{
    {"ctrl", wf::KEYBOARD_MODIFIER_CTRL},
    {"alt", wf::KEYBOARD_MODIFIER_ALT},
    {"shift", wf::KEYBOARD_MODIFIER_SHIFT},
    {"super", wf::KEYBOARD_MODIFIER_LOGO},
};

static std::string binding_to_string(general_binding_t binding)
{
    std::string result = "";
    for (auto& pair : modifier_names)
    {
        if (binding.mods & pair.second)
        {
            result += "<" + pair.first + "> ";
        }
    }

    if (binding.value > 0)
    {
        auto evdev_name = libevdev_event_code_get_name(EV_KEY, binding.value);
        result += evdev_name ?: "NULL";
    }

    return result;
}

/**
 * @return A string which consists of the characters of value, but without
 *  those contained in filter.
 */
static std::string filter_out(std::string value, std::string filter)
{
    std::string result;
    for (auto& c : value)
    {
        if (filter.find(c) != std::string::npos)
        {
            continue;
        }

        result += c;
    }

    return result;
}

static const std::string whitespace_chars = " \t\n\r\v\b";

static std::optional<general_binding_t> parse_binding(
    std::string binding_description)
{
    /* Handle disabled bindings */
    auto binding_descr_no_whitespace =
        filter_out(binding_description, whitespace_chars);
    if ((binding_descr_no_whitespace == "none") ||
        (binding_descr_no_whitespace == "disabled"))
    {
        return general_binding_t{false, 0, 0};
    }

    /* Strategy: split the binding at modifier begin/end markings and spaces,
     * and then drop empty tokens. The tokens that are left should be either a
     * binding or something recognizable by evdev. */
    static const std::string delims = "<>" + whitespace_chars;
    auto tokens = split_at(binding_description, delims);
    if (tokens.empty())
    {
        return {};
    }

    general_binding_t result = {true, 0, 0};
    for (size_t i = 0; i < tokens.size() - 1; i++)
    {
        if (modifier_names.count(tokens[i]))
        {
            result.mods |= modifier_names[tokens[i]];
        } else
        {
            return {}; // invalid modifier
        }
    }

    int code = libevdev_event_code_from_name(EV_KEY, tokens.back().c_str());
    if (code == -1)
    {
        /* Last token might either be yet another modifier (in case of modifier
         * bindings) or it may be KEY_*. If neither, we have invalid binding */
        if (modifier_names.count(tokens.back()))
        {
            result.mods |= modifier_names[tokens.back()];
            code = 0;
        } else
        {
            return {}; // not found
        }
    }

    result.value = code;

    /* Do one last check: if we remove whitespaces, and add a whitespace after
     * each > character, then the resulting string should be almost equal to the
     * minimal description, generated by binding_to_string().
     *
     * Since we have already checked all identifiers and they are valid
     * modifiers, it is enough to check just that the lengths are matching.
     * Note we can't directly compare because the order may be different. */
    auto filtered_descr = filter_out(binding_description, whitespace_chars);
    std::string filtered_with_spaces;
    for (auto c : filtered_descr)
    {
        filtered_with_spaces += c;
        if (c == '>')
        {
            filtered_with_spaces += " ";
        }
    }

    auto minimal_descr = binding_to_string(result);
    if (filtered_with_spaces.length() == minimal_descr.length())
    {
        return result;
    }

    return {};
}

wf::keybinding_t::keybinding_t(uint32_t modifier, uint32_t keyval)
{
    this->mod    = modifier;
    this->keyval = keyval;
}

template<>
std::optional<wf::keybinding_t> wf::option_type::from_string(
    const std::string& description)
{
    auto parsed_opt = parse_binding(description);
    if (!parsed_opt)
    {
        return {};
    }

    auto parsed = parsed_opt.value();

    /* Disallow buttons, because evdev treats buttons and keys the same */
    if (parsed.enabled && (parsed.value > 0) &&
        (description.find("KEY") == std::string::npos))
    {
        return {};
    }

    if (parsed.enabled && (parsed.mods == 0) && (parsed.value == 0))
    {
        return {};
    }

    return wf::keybinding_t{parsed.mods, parsed.value};
}

template<>
std::string wf::option_type::to_string(const wf::keybinding_t& value)
{
    if ((value.get_modifiers() == 0) && (value.get_key() == 0))
    {
        return "none";
    }

    return binding_to_string({true, value.get_modifiers(), value.get_key()});
}

bool wf::keybinding_t::operator ==(const keybinding_t& other) const
{
    return this->mod == other.mod && this->keyval == other.keyval;
}

/** @return The modifiers of the keybinding */
uint32_t wf::keybinding_t::get_modifiers() const
{
    return this->mod;
}

/** @return The key of the keybinding */
uint32_t wf::keybinding_t::get_key() const
{
    return this->keyval;
}

/* -------------------------- wf::buttonbinding_t --------------------------- */
wf::buttonbinding_t::buttonbinding_t(uint32_t modifier, uint32_t buttonval)
{
    this->mod    = modifier;
    this->button = buttonval;
}

template<>
std::optional<wf::buttonbinding_t> wf::option_type::from_string(
    const std::string& description)
{
    auto parsed_opt = parse_binding(description);
    if (!parsed_opt)
    {
        return {};
    }

    auto parsed = parsed_opt.value();
    if (!parsed.enabled)
    {
        return wf::buttonbinding_t{0, 0};
    }

    /* Disallow keys, because evdev treats buttons and keys the same */
    if (description.find("BTN") == std::string::npos)
    {
        return {};
    }

    if (parsed.value == 0)
    {
        return {};
    }

    return wf::buttonbinding_t{parsed.mods, parsed.value};
}

template<>
std::string wf::option_type::to_string(
    const wf::buttonbinding_t& value)
{
    if ((value.get_modifiers() == 0) && (value.get_button() == 0))
    {
        return "none";
    }

    return binding_to_string({true, value.get_modifiers(), value.get_button()});
}

bool wf::buttonbinding_t::operator ==(const buttonbinding_t& other) const
{
    return this->mod == other.mod && this->button == other.button;
}

uint32_t wf::buttonbinding_t::get_modifiers() const
{
    return this->mod;
}

uint32_t wf::buttonbinding_t::get_button() const
{
    return this->button;
}

wf::touchgesture_t::touchgesture_t(touch_gesture_type_t type, uint32_t direction,
    int finger_count)
{
    this->type = type;
    this->direction    = direction;
    this->finger_count = finger_count;
}

static const std::map<std::string, wf::touch_gesture_direction_t>
touch_gesture_direction_string_map =
{
    {"up", wf::GESTURE_DIRECTION_UP},
    {"down", wf::GESTURE_DIRECTION_DOWN},
    {"left", wf::GESTURE_DIRECTION_LEFT},
    {"right", wf::GESTURE_DIRECTION_RIGHT}
};

static wf::touch_gesture_direction_t parse_single_direction(
    const std::string& direction)
{
    if (touch_gesture_direction_string_map.count(direction))
    {
        return touch_gesture_direction_string_map.at(direction);
    }

    throw std::domain_error("invalid swipe direction");
}

uint32_t parse_direction(const std::string& direction)
{
    size_t hyphen = direction.find("-");
    if (hyphen == std::string::npos)
    {
        return parse_single_direction(direction);
    } else
    {
        /* we support up to 2 directions, because >= 3 will be invalid anyway */
        auto first  = direction.substr(0, hyphen);
        auto second = direction.substr(hyphen + 1);

        uint32_t mask =
            parse_single_direction(first) | parse_single_direction(second);

        const uint32_t both_horiz =
            wf::GESTURE_DIRECTION_LEFT | wf::GESTURE_DIRECTION_RIGHT;
        const uint32_t both_vert =
            wf::GESTURE_DIRECTION_UP | wf::GESTURE_DIRECTION_DOWN;

        if (((mask & both_horiz) == both_horiz) ||
            ((mask & both_vert) == both_vert))
        {
            throw std::domain_error("Cannot have two opposing directions in the"
                                    "same gesture");
        }

        return mask;
    }
}

wf::touchgesture_t parse_gesture(const std::string& value)
{
    if (value.empty())
    {
        return {wf::GESTURE_TYPE_NONE, 0, 0};
    }

    auto tokens = split_at(value, " \t\v\b\n\r");
    assert(!tokens.empty());

    if (tokens.size() != 3)
    {
        return {wf::GESTURE_TYPE_NONE, 0, 0};
    }

    try {
        wf::touch_gesture_type_t type;
        uint32_t direction = 0;
        int32_t finger_count;

        if (tokens[0] == "pinch")
        {
            type = wf::GESTURE_TYPE_PINCH;
            if (tokens[1] == "in")
            {
                direction = wf::GESTURE_DIRECTION_IN;
            } else if (tokens[1] == "out")
            {
                direction = wf::GESTURE_DIRECTION_OUT;
            } else
            {
                throw std::domain_error("Invalid pinch direction: " + tokens[1]);
            }
        } else if (tokens[0] == "swipe")
        {
            type = wf::GESTURE_TYPE_SWIPE;
            direction = parse_direction(tokens[1]);
        } else if (tokens[0] == "edge-swipe")
        {
            type = wf::GESTURE_TYPE_EDGE_SWIPE;
            direction = parse_direction(tokens[1]);
        } else
        {
            throw std::domain_error("Invalid gesture type:" + tokens[0]);
        }

        // TODO: instead of atoi, check properly
        finger_count = std::atoi(tokens[2].c_str());
        return wf::touchgesture_t{type, direction, finger_count};
    } catch (std::exception& e)
    {
        // XXX: show error?
        // ignore it, will return GESTURE_NONE
    }

    return wf::touchgesture_t{wf::GESTURE_TYPE_NONE, 0, 0};
}

template<>
std::optional<wf::touchgesture_t> wf::option_type::from_string(
    const std::string& description)
{
    auto as_binding = parse_binding(description);
    if (as_binding && !as_binding.value().enabled)
    {
        return touchgesture_t{GESTURE_TYPE_NONE, 0, 0};
    }

    auto gesture = parse_gesture(description);
    if (gesture.get_type() == GESTURE_TYPE_NONE)
    {
        return {};
    }

    return gesture;
}

static std::string direction_to_string(uint32_t direction)
{
    std::string result = "";
    for (auto& pair : touch_gesture_direction_string_map)
    {
        if (direction & pair.second)
        {
            result += pair.first + "-";
        }
    }

    if (result.size() > 0)
    {
        /* Remove trailing - */
        result.pop_back();
    }

    return result;
}

template<>
std::string wf::option_type::to_string(const touchgesture_t& value)
{
    std::string result;
    switch (value.get_type())
    {
      case GESTURE_TYPE_NONE:
        return "";

      case GESTURE_TYPE_EDGE_SWIPE:
        result += "edge-";

      // fallthrough
      case GESTURE_TYPE_SWIPE:
        result += "swipe ";
        result += direction_to_string(value.get_direction()) + " ";
        break;

      case GESTURE_TYPE_PINCH:
        result += "pinch ";

        if (value.get_direction() == GESTURE_DIRECTION_IN)
        {
            result += "in ";
        }

        if (value.get_direction() == GESTURE_DIRECTION_OUT)
        {
            result += "out ";
        }

        break;
    }

    result += wf::option_type::to_string(value.get_finger_count());
    return result;
}

wf::touch_gesture_type_t wf::touchgesture_t::get_type() const
{
    return this->type;
}

int wf::touchgesture_t::get_finger_count() const
{
    return this->finger_count;
}

uint32_t wf::touchgesture_t::get_direction() const
{
    return this->direction;
}

bool wf::touchgesture_t::operator ==(const touchgesture_t& other) const
{
    return type == other.type && finger_count == other.finger_count &&
           (direction == 0 || other.direction == 0 || direction == other.direction);
}

/* --------------------------- activatorbinding_t --------------------------- */
struct wf::activatorbinding_t::impl
{
    std::vector<keybinding_t> keys;
    std::vector<buttonbinding_t> buttons;
    std::vector<touchgesture_t> gestures;
    std::vector<hotspot_binding_t> hotspots;
    std::vector<std::string> extensions;
};

wf::activatorbinding_t::activatorbinding_t()
{
    this->priv = std::make_unique<impl>();
}

wf::activatorbinding_t::~activatorbinding_t() = default;

wf::activatorbinding_t::activatorbinding_t(const activatorbinding_t& other)
{
    this->priv = std::make_unique<impl>(*other.priv);
}

wf::activatorbinding_t& wf::activatorbinding_t::operator =(
    const activatorbinding_t& other)
{
    if (&other != this)
    {
        this->priv = std::make_unique<impl>(*other.priv);
    }

    return *this;
}

template<class Type>
bool try_add_binding(
    std::vector<Type>& to, const std::string& value)
{
    auto binding = wf::option_type::from_string<Type>(value);
    if (binding)
    {
        to.push_back(binding.value());
        return true;
    }

    return false;
}

static std::string filter_trailing_leading(std::string token)
{
    while (!token.empty() && token.back() == ' ')
    {
        token.pop_back();
    }

    while (!token.empty() && token.front() == ' ')
    {
        token.erase(token.begin());
    }

    return token;
}

template<>
std::optional<wf::activatorbinding_t> wf::option_type::from_string(
    const std::string& string)
{
    activatorbinding_t binding;

    if (filter_out(string, whitespace_chars) == "")
    {
        return binding; // empty binding
    }

    auto tokens = split_at(string, "|", true);
    for (auto& token : tokens)
    {
        const bool only_whitespace = std::all_of(token.begin(), token.end(),
            [] (char c) { return std::isspace(c); });
        if (only_whitespace)
        {
            return {};
        }

        bool is_valid_binding =
            try_add_binding(binding.priv->keys, token) ||
            try_add_binding(binding.priv->buttons, token) ||
            try_add_binding(binding.priv->gestures, token) ||
            try_add_binding(binding.priv->hotspots, token);

        if (!is_valid_binding)
        {
            binding.priv->extensions.push_back(filter_trailing_leading(token));
        }
    }

    return binding;
}

template<class Type>
static std::string concatenate_bindings(const std::vector<Type>& bindings)
{
    std::string repr = "";
    for (auto& b : bindings)
    {
        repr += wf::option_type::to_string<Type>(b);
        repr += " | ";
    }

    return repr;
}

template<>
std::string wf::option_type::to_string(
    const activatorbinding_t& value)
{
    std::string repr =
        concatenate_bindings(value.priv->keys) +
        concatenate_bindings(value.priv->buttons) +
        concatenate_bindings(value.priv->gestures) +
        concatenate_bindings(value.priv->hotspots);

    /* Remove trailing " | " */
    if (repr.size() >= 3)
    {
        repr.erase(repr.size() - 3);
    }

    return repr;
}

template<class Type>
bool find_in_container(const std::vector<Type>& haystack,
    Type needle)
{
    return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
}

bool wf::activatorbinding_t::has_match(const keybinding_t& key) const
{
    return find_in_container(priv->keys, key);
}

bool wf::activatorbinding_t::has_match(const buttonbinding_t& button) const
{
    return find_in_container(priv->buttons, button);
}

bool wf::activatorbinding_t::has_match(const touchgesture_t& gesture) const
{
    return find_in_container(priv->gestures, gesture);
}

bool wf::activatorbinding_t::operator ==(const activatorbinding_t& other) const
{
    if (other.get_extensions().size() != this->get_extensions().size())
    {
        return false;
    }

    for (size_t i = 0; i < other.get_extensions().size(); i++)
    {
        if (other.get_extensions()[i] != this->get_extensions()[i])
        {
            return false;
        }
    }

    return priv->keys == other.priv->keys &&
           priv->buttons == other.priv->buttons &&
           priv->gestures == other.priv->gestures &&
           priv->hotspots == other.priv->hotspots;
}

const std::vector<std::string>& wf::activatorbinding_t::get_extensions() const
{
    return priv->extensions;
}

const std::vector<wf::hotspot_binding_t>& wf::activatorbinding_t::get_hotspots()
const
{
    return priv->hotspots;
}

wf::hotspot_binding_t::hotspot_binding_t(uint32_t edges,
    int32_t along_edge, int32_t away_from_edge, int32_t timeout)
{
    this->edges   = edges;
    this->along   = along_edge;
    this->away    = away_from_edge;
    this->timeout = timeout;
}

bool wf::hotspot_binding_t::operator ==(const hotspot_binding_t& other) const
{
    return edges == other.edges && along == other.along && away == other.away &&
           timeout == other.timeout;
}

int32_t wf::hotspot_binding_t::get_timeout() const
{
    return timeout;
}

int32_t wf::hotspot_binding_t::get_size_away_from_edge() const
{
    return away;
}

int32_t wf::hotspot_binding_t::get_size_along_edge() const
{
    return along;
}

uint32_t wf::hotspot_binding_t::get_edges() const
{
    return edges;
}

static std::map<std::string, wf::output_edge_t> hotspot_edges =
{
    {"top", wf::OUTPUT_EDGE_TOP},
    {"bottom", wf::OUTPUT_EDGE_BOTTOM},
    {"left", wf::OUTPUT_EDGE_LEFT},
    {"right", wf::OUTPUT_EDGE_RIGHT},
};

template<>
std::optional<wf::hotspot_binding_t> wf::option_type::from_string(
    const std::string& description)
{
    std::istringstream stream{description};
    std::string token;
    stream >> token; // "hotspot"
    if (token != "hotspot")
    {
        return {};
    }

    stream >> token; // direction

    uint32_t edges = 0;

    size_t hyphen = token.find("-");
    if (hyphen == token.npos)
    {
        if (hotspot_edges.count(token) == 0)
        {
            return {};
        }

        edges = hotspot_edges[token];
    } else
    {
        std::string first_direction  = token.substr(0, hyphen);
        std::string second_direction = token.substr(hyphen + 1);

        if ((hotspot_edges.count(first_direction) == 0) ||
            (hotspot_edges.count(second_direction) == 0))
        {
            return {};
        }

        edges = hotspot_edges[first_direction] | hotspot_edges[second_direction];
    }

    stream >> token;
    int32_t along, away;
    if (2 != sscanf(token.c_str(), "%dx%d", &along, &away))
    {
        return {};
    }

    stream >> token;
    auto timeout = wf::option_type::from_string<int>(token);

    if (!timeout || stream >> token) // check for trailing characters
    {
        return {};
    }

    return wf::hotspot_binding_t(edges, along, away, timeout.value());
}

template<>
std::string wf::option_type::to_string(
    const wf::hotspot_binding_t& value)
{
    std::ostringstream out;
    out << "hotspot ";

    uint32_t remaining_edges = value.get_edges();

    const auto& find_edge = [&] (bool need_hyphen)
    {
        for (const auto& edge : hotspot_edges)
        {
            if (remaining_edges & edge.second)
            {
                remaining_edges &= ~edge.second;
                if (need_hyphen)
                {
                    out << "-";
                }

                out << edge.first;
                break;
            }
        }
    };

    find_edge(false);
    find_edge(true);

    out << " " << value.get_size_along_edge() << "x" <<
        value.get_size_away_from_edge() <<
        " " << value.get_timeout();
    return out.str();
}

/* ------------------------- Output config types ---------------------------- */
wf::output_config::mode_t::mode_t(bool auto_on)
{
    this->type = auto_on ? MODE_AUTO : MODE_OFF;
}

wf::output_config::mode_t::mode_t(int prioritize_res)
{
    this->type = prioritize_res == 3 ? MODE_HIGHRES : MODE_HIGHRR;
}

wf::output_config::mode_t::mode_t(int32_t width, int32_t height, int32_t refresh)
{
    this->type    = MODE_RESOLUTION;
    this->width   = width;
    this->height  = height;
    this->refresh = refresh;
}

/**
 * Initialize a mirror mode.
 */
wf::output_config::mode_t::mode_t(const std::string& mirror_from)
{
    this->type = MODE_MIRROR;
    this->mirror_from = mirror_from;
}

/** @return The type of this mode. */
wf::output_config::mode_type_t wf::output_config::mode_t::get_type() const
{
    return type;
}

int32_t wf::output_config::mode_t::get_width() const
{
    return width;
}

int32_t wf::output_config::mode_t::get_height() const
{
    return height;
}

int32_t wf::output_config::mode_t::get_refresh() const
{
    return refresh;
}

std::string wf::output_config::mode_t::get_mirror_from() const
{
    return mirror_from;
}

bool wf::output_config::mode_t::operator ==(const mode_t& other) const
{
    if (type != other.get_type())
    {
        return false;
    }

    switch (type)
    {
      case MODE_RESOLUTION:
        return width == other.width && height == other.height &&
               refresh == other.refresh;

      case MODE_MIRROR:
        return mirror_from == other.mirror_from;

      case MODE_AUTO:
      case MODE_HIGHRR:
      case MODE_HIGHRES:
      case MODE_OFF:
        return true;
    }

    return false;
}

template<>
std::optional<wf::output_config::mode_t> wf::option_type::from_string(
    const std::string& string)
{
    if (string == "off")
    {
        return wf::output_config::mode_t{false};
    }

    if ((string == "auto") || (string == "default"))
    {
        return wf::output_config::mode_t{true};
    }

    if ((string == "highres"))
    {
        return wf::output_config::mode_t{3};
    }

    if ((string == "highrr"))
    {
        return wf::output_config::mode_t{2};
    }

    if (string.substr(0, 6) == "mirror")
    {
        std::stringstream ss(string);
        std::string from, dummy;
        ss >> from; // the mirror word
        if (!(ss >> from))
        {
            return {};
        }

        // trailing garbage
        if (ss >> dummy)
        {
            return {};
        }

        return wf::output_config::mode_t{from};
    }

    int w, h, rr = 0;
    char next;

    int read = std::sscanf(string.c_str(), "%d x %d @ %d%c", &w, &h, &rr, &next);
    if ((read < 2) || (read > 3))
    {
        return {};
    }

    if ((w < 0) || (h < 0) || (rr < 0))
    {
        return {};
    }

    // Ensure refresh rate in mHz
    if (rr < 1000)
    {
        rr *= 1000;
    }

    return wf::output_config::mode_t{w, h, rr};
}

/** Represent the activator binding as a string. */
template<>
std::string wf::option_type::to_string(const output_config::mode_t& value)
{
    switch (value.get_type())
    {
      case output_config::MODE_AUTO:
        return "auto";

      case output_config::MODE_HIGHRR:
        return "highrr";

      case output_config::MODE_HIGHRES:
        return "highres";

      case output_config::MODE_OFF:
        return "off";

      case output_config::MODE_RESOLUTION:
        if (value.get_refresh() <= 0)
        {
            return to_string(value.get_width()) + "x" +
                   to_string(value.get_height());
        } else
        {
            return to_string(value.get_width()) + "x" +
                   to_string(value.get_height()) + "@" + to_string(
                value.get_refresh());
        }

      case output_config::MODE_MIRROR:
        return "mirror " + value.get_mirror_from();
    }

    return {};
}

wf::output_config::position_t::position_t()
{
    this->automatic = true;
}

wf::output_config::position_t::position_t(int32_t x, int32_t y)
{
    this->automatic = false;
    this->x = x;
    this->y = y;
}

int32_t wf::output_config::position_t::get_x() const
{
    return x;
}

int32_t wf::output_config::position_t::get_y() const
{
    return y;
}

bool wf::output_config::position_t::is_automatic_position() const
{
    return automatic;
}

bool wf::output_config::position_t::operator ==(const position_t& other) const
{
    if (is_automatic_position() != other.is_automatic_position())
    {
        return false;
    }

    if (is_automatic_position())
    {
        return true;
    }

    return x == other.x && y == other.y;
}

template<>
std::optional<wf::output_config::position_t> wf::option_type::from_string(
    const std::string& string)
{
    if ((string == "auto") || (string == "default"))
    {
        return wf::output_config::position_t();
    }

    int x, y;
    char r;
    if (sscanf(string.c_str(), "%d , %d%c", &x, &y, &r) != 2)
    {
        return {};
    }

    return wf::output_config::position_t(x, y);
}

/** Represent the activator binding as a string. */
template<>
std::string wf::option_type::to_string(const output_config::position_t& value)
{
    if (value.is_automatic_position())
    {
        return "auto";
    }

    return to_string(value.get_x()) + ", " + to_string(value.get_y());
}

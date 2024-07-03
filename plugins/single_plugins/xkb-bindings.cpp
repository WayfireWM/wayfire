#include "wayfire/signal-definitions.hpp"
#include "wayfire/signal-provider.hpp"
#include <wayfire/plugin.hpp>
#include <wayfire/bindings-repository.hpp>
#include "wayfire/core.hpp"
#include "wayfire/seat.hpp"
#include <map>

namespace wf
{
static std::string to_lower(std::string str)
{
    std::transform(str.begin(), str.end(), str.begin(), tolower);
    return str;
}

static const std::map<std::string, uint32_t> mod_name_to_mod = {
    {to_lower(XKB_MOD_NAME_SHIFT), WLR_MODIFIER_SHIFT},
    {to_lower(XKB_MOD_NAME_CAPS), WLR_MODIFIER_CAPS},
    {to_lower(XKB_MOD_NAME_CTRL), WLR_MODIFIER_CTRL},
    {"ctrl", WLR_MODIFIER_CTRL},
    {to_lower(XKB_MOD_NAME_ALT), WLR_MODIFIER_ALT},
    {"alt", WLR_MODIFIER_ALT},
    {to_lower(XKB_MOD_NAME_NUM), WLR_MODIFIER_MOD2},
    {"mod3", WLR_MODIFIER_MOD3},
    {to_lower(XKB_MOD_NAME_LOGO), WLR_MODIFIER_LOGO},
    {"mod5", WLR_MODIFIER_MOD5},
};

static uint32_t parse_modifier(std::string mod_name)
{
    auto it = mod_name_to_mod.find(to_lower(mod_name));
    if (it != mod_name_to_mod.end())
    {
        return it->second;
    }

    return 0;
}

std::vector<std::string> tokenize_at(std::string str, char token)
{
    std::vector<std::string> tokens;
    std::istringstream iss(str);
    std::string token_str;
    while (std::getline(iss, token_str, token))
    {
        tokens.push_back(token_str);
    }

    return tokens;
}

struct xkb_binding_t
{
    uint32_t mods;
    std::string keysym;

    bool operator ==(const xkb_binding_t& other) const
    {
        return (mods == other.mods) && (keysym == other.keysym);
    }
};

static std::optional<xkb_binding_t> parse_keybinding_xkb(std::string binding)
{
    binding.erase(std::remove(binding.begin(), binding.end(), ' '), binding.end());
    auto tokens = tokenize_at(binding, '+');
    if (tokens.empty())
    {
        return std::nullopt;
    }

    uint32_t mods = 0;
    for (size_t i = 0; i < tokens.size() - 1; i++)
    {
        auto mod = parse_modifier(tokens[i]);
        if (!mod)
        {
            return std::nullopt;
        }

        mods |= mod;
    }

    return xkb_binding_t{mods, tokens.back()};
}

class xkb_bindings_t : public wf::plugin_interface_t
{
  public:
    void init() override
    {
        wf::get_core().connect(&on_parse_extension);
        wf::get_core().connect(&on_keyboard_key);
        wf::get_core().bindings->reparse_extensions();
    }

    void fini() override
    {
        on_parse_extension.disconnect();
        wf::get_core().bindings->reparse_extensions();
    }

    wf::signal::connection_t<parse_activator_extension_signal> on_parse_extension =
        [=] (parse_activator_extension_signal *ev)
    {
        if (auto binding = parse_keybinding_xkb(ev->extension_binding))
        {
            ev->tag = binding.value();
        }
    };

    wf::signal::connection_t<input_event_signal<wlr_keyboard_key_event>> on_keyboard_key =
        [=] (input_event_signal<wlr_keyboard_key_event> *ev)
    {
        if (!ev->device || (ev->mode == wf::input_event_processing_mode_t::IGNORE) ||
            (ev->event->state != WL_KEYBOARD_KEY_STATE_PRESSED))
        {
            return;
        }

        auto keyboard = wlr_keyboard_from_input_device(ev->device);

        xkb_keysym_t sym = xkb_state_key_get_one_sym(keyboard->xkb_state, ev->event->keycode + 8);
        if (sym == XKB_KEY_NoSymbol)
        {
            return;
        }

        static constexpr size_t max_keysym_len = 128;
        char buffer[max_keysym_len];

        size_t len = xkb_keysym_get_name(sym, buffer, max_keysym_len);
        std::string keysym = std::string(buffer, len);
        uint32_t mods = wf::get_core().seat->get_keyboard_modifiers();

        wf::activator_data_t data = {
            .source = wf::activator_source_t::KEYBINDING,
            .activation_data = ev->event->keycode
        };

        if (wf::get_core().bindings->handle_extension(xkb_binding_t{mods, keysym}, data))
        {
            ev->mode = wf::input_event_processing_mode_t::NO_CLIENT;
        }
    };
};
}

DECLARE_WAYFIRE_PLUGIN(wf::xkb_bindings_t);

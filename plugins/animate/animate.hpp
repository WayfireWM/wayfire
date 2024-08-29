#ifndef ANIMATE_H_
#define ANIMATE_H_

#include <wayfire/view.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/option-wrapper.hpp>

#define WF_ANIMATE_HIDING_ANIMATION (1 << 0)
#define WF_ANIMATE_SHOWING_ANIMATION (1 << 1)
#define WF_ANIMATE_MAP_STATE_ANIMATION (1 << 2)
#define WF_ANIMATE_MINIMIZE_STATE_ANIMATION (1 << 3)

namespace wf
{
namespace animate
{
enum animation_type
{
    ANIMATION_TYPE_MAP      = WF_ANIMATE_SHOWING_ANIMATION | WF_ANIMATE_MAP_STATE_ANIMATION,
    ANIMATION_TYPE_UNMAP    = WF_ANIMATE_HIDING_ANIMATION | WF_ANIMATE_MAP_STATE_ANIMATION,
    ANIMATION_TYPE_MINIMIZE = WF_ANIMATE_HIDING_ANIMATION | WF_ANIMATE_MINIMIZE_STATE_ANIMATION,
    ANIMATION_TYPE_RESTORE  = WF_ANIMATE_SHOWING_ANIMATION | WF_ANIMATE_MINIMIZE_STATE_ANIMATION,
};

class animation_base_t
{
  public:
    virtual void init(wayfire_view view, wf::animation_description_t duration, animation_type type)
    {}

    /**
     * @return True if the animation should continue for at least one more frame.
     */
    virtual bool step()
    {
        return false;
    }

    /**
     * Reverse the animation direction (hiding -> showing, showing -> hiding)
     */
    virtual void reverse()
    {}

    virtual ~animation_base_t() = default;
};

struct effect_description_t
{
    std::function<std::unique_ptr<animation_base_t>()> generator;
    std::function<std::optional<wf::animation_description_t>()> default_duration;
};

/**
 * The effects registry holds a list of all available animation effects.
 * Plugins can access the effects registry via ref_ptr_t helper in wayfire/plugins/common/shared-core-data.hpp
 * They may add/remove their own effects.
 */
class animate_effects_registry_t
{
  public:
    void register_effect(std::string name, effect_description_t effect)
    {
        effects[name] = effect;
    }

    void unregister_effect(std::string name)
    {
        effects.erase(name);
    }

    std::map<std::string, effect_description_t> effects;
};
}
}

#endif

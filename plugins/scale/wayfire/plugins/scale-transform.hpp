#pragma once

#include <wayfire/view-transform.hpp>
#include <wayfire/nonstd/observer_ptr.h>
#include <wayfire/render-manager.hpp>
#include <string>
#include <list>
#include <algorithm>

namespace wf
{
/**
 * transformer used by scale -- it is an extension of the 2D transformer
 * with the ability to add overlays
 */
class scale_transformer_t : public wf::view_2D
{
  public:
    scale_transformer_t(wayfire_view view) : wf::view_2D(view)
    {}
    ~scale_transformer_t()
    {}

    struct padding_t
    {
        unsigned int top    = 0;
        unsigned int left   = 0;
        unsigned int bottom = 0;
        unsigned int right  = 0;

        /**
         * Compare paddings. Returns true if ANY of the dimensions in this
         * is smaller than other. Note that this does not define an ordering
         * on paddings.
         */
        bool any_smaller_than(const padding_t& other) const
        {
            return (top < other.top) || (left < other.left) ||
                   (bottom < other.bottom) || (right < other.right);
        }

        /**
         * Helper function to extend padding to ensure it is at least as
         * large as other.
         */
        void extend(const padding_t& other)
        {
            top    = std::max(top, other.top);
            left   = std::max(left, other.left);
            bottom = std::max(bottom, other.bottom);
            right  = std::max(right, other.right);
        }
    };

    uint32_t get_z_order() override
    {
        return wf::TRANSFORMER_HIGHLEVEL - 10;
    }

    /**
     * Effect hook called in the following circumstances:
     *  (1) as pre-render hooks while this transformer is attached to the view
     *  (2) if another transformer changed the view's size, so the overlay and
     * padding might need to be updated
     *
     * The second case can happen just during rendering the view (from
     * view::render_transformed()), so it should not call damage.
     *
     * @return Whether the overlay changed. In this case damage will be
     * scheduled for the view.
     *
     * Damage will also be scheduled if the size of the combined padding from
     * all overlays have changed.
     */
    using pre_hook_t = std::function<bool (void)>;

    /**
     * Render hook for drawing on top of the surface after the transform, during
     * rendering the view.
     *
     * @param fb     The framebuffer to render to.
     * @param damage Damaged region to render.
     */
    using render_hook_t = std::function<void (const wf::render_target_t& fb,
        const wf::region_t& damage)>;

    /**
     * Overlays that can be added to this transformer. Hooks are called
     * similarly to render-manager.
     */
    struct overlay_t
    {
        /* Pre hook; called just before rendering, can adjust padding. Return
         * value indicates if damage should be scheduled for the view. */
        pre_hook_t pre_hook;
        /* Render hook; called during rendering, after this transform has been
         * applied to the view. This can only render to the view's texture. */
        render_hook_t render_hook;
        /* Extra padding around the transformed view required by this overlay.
         * This is added to the view's bounding box */
        padding_t view_padding;
        /* Extra padding taken to be taken into consideration by scale's layout.
         * This can differ from view_padding e.g. if this overlay is rendering
         * directly to the end framebuffer */
        padding_t scale_padding;

        virtual ~overlay_t() = default;
    };

    /* render the transformed view and then add all overlays */
    void render_with_damage(wf::texture_t src_tex, wlr_box src_box,
        const wf::region_t& damage, const wf::render_target_t& target_fb) override
    {
        /* render the transformed view first */
        view_transformer_t::render_with_damage(src_tex, src_box, damage, target_fb);

        /* call all overlays */
        for (auto& p : overlays)
        {
            auto& ol = *(p.second);
            if (ol.render_hook)
            {
                ol.render_hook(target_fb, damage);
            }
        }
    }

    /**
     * Call pre-render hooks.
     *
     * Will also damage the view if either the parameter is true or if the
     * total padding changes or if any of the overlays explicitly requests it.
     */
    void call_pre_hooks(bool damage_request)
    {
        call_pre_hooks(damage_request, true);
    }

    /**
     * Add a new overlay that is rendered after this transform.
     *
     * @param ol      The overlay object to be added.
     * @param z_order Relative order; overlays are called in order.
     */
    void add_overlay(std::unique_ptr<overlay_t>&& ol, int z_order)
    {
        auto it = std::find_if(overlays.begin(), overlays.end(),
            [z_order] (const auto& other)
        {
            return other.first >= z_order;
        });

        view_padding.extend(ol->view_padding);
        scale_padding.extend(ol->scale_padding);
        overlays.insert(it, std::pair<int, std::unique_ptr<overlay_t>>(z_order,
            std::move(ol)));
        view->damage();
    }

    /* remove an existing overlay */
    void rem_overlay(nonstd::observer_ptr<overlay_t> ol)
    {
        view->damage();
        overlays.remove_if([ol] (const auto& other)
        {
            return other.second.get() == ol.get();
        });

        recalculate_padding();
        view->damage();
    }

    /* get the view being transformed (it is protected in view_2D) */
    wayfire_view get_transformed_view() const
    {
        return view;
    }

    /**
     * Transformer name used by scale. This can be used by other plugins to find
     * scale's transformer on a view.
     */
    static std::string transformer_name()
    {
        return "scale";
    }

    /**
     * Transform a box, including the current transform, but not the padding.
     */
    wlr_box trasform_box_without_padding(wlr_box box)
    {
        box = view->transform_region(box, this);
        wlr_box view_box = view->get_bounding_box(this);
        return view_transformer_t::get_bounding_box(view_box, box);
    }

    /**
     * Transform the view's bounding box, including the current transform, but not
     * the padding.
     */
    wlr_box transform_bounding_box_without_padding()
    {
        auto box = view->get_bounding_box(this);
        return view_transformer_t::get_bounding_box(box, box);
    }

    /**
     * Transform a region and add padding to it.
     * Note: this will pad any transformed region, not only if it corresponds to
     * the view's bounding box.
     */
    wlr_box get_bounding_box(wf::geometry_t view, wlr_box region) override
    {
        if (view != last_view_box)
        {
            /* box changed, we might need to update our padding;
             * this can happen if another transformer was removed between
             * pre-render hooks and rendering; in this case, the code removing
             * the other transformer should call damage() before and after,
             * which in turn will call this function; there is no need to
             * call damage() here */
            last_view_box = view;
            call_pre_hooks(false, false);
        }

        region    = view_transformer_t::get_bounding_box(view, region);
        region.x -= view_padding.left;
        region.y -= view_padding.top;
        region.width  += view_padding.left + view_padding.right;
        region.height += view_padding.top + view_padding.bottom;
        return region;
    }

    const padding_t& get_scale_padding() const
    {
        return scale_padding;
    }

  protected:
    /* list of active overlays */
    std::list<std::pair<int, std::unique_ptr<overlay_t>>> overlays;
    padding_t view_padding; /* combined padding added to the view's bounding box */
    padding_t scale_padding; /* combined padding to be used by scale's layout */

    /* recalculate padding */
    void recalculate_padding()
    {
        view_padding  = {0, 0, 0, 0};
        scale_padding = {0, 0, 0, 0};
        for (const auto& p : overlays)
        {
            view_padding.extend(p.second->view_padding);
            scale_padding.extend(p.second->scale_padding);
        }
    }

    void call_pre_hooks(bool damage_request, bool can_damage)
    {
        padding_t new_view_pad;
        scale_padding = {0, 0, 0, 0};
        for (auto& p : overlays)
        {
            auto& ol = *(p.second);
            if (ol.pre_hook)
            {
                damage_request |= ol.pre_hook();

                new_view_pad.extend(ol.view_padding);
                scale_padding.extend(ol.scale_padding);
            }
        }

        /* Note: if some dimensions of the padding have shrunk, while others
        * have grown, we need to call damage() twice (once with the old, once
        * with the new padding), to include the whole box. This could be
        * avoided by calculating a box that contains both old and new padding
        * and calling damage directly on the output (after transforming). */
        bool padding_shrunk = new_view_pad.any_smaller_than(view_padding);
        bool padding_grown  = view_padding.any_smaller_than(new_view_pad);

        if (padding_shrunk)
        {
            if (can_damage)
            {
                view->damage();
            }

            view_padding = new_view_pad;
            /* no need to damage in the next step unless some dimensions
             * have grown */
            damage_request = false;
        }

        if (padding_grown || damage_request)
        {
            view_padding = new_view_pad;
            if (can_damage)
            {
                view->damage();
            }
        }
    }

    wf::geometry_t last_view_box = {0, 0, 0, 0};
    wf::wl_idle_call idle_call;
};
}

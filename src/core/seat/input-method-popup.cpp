#include <wayfire/unstable/wlr-text-input-v3-popup.hpp>
#include "../../view/view-impl.hpp"
#include "wayfire/scene-operations.hpp"

wf::text_input_v3_popup::text_input_v3_popup(wf::text_input_v3_im_relay_interface_t *rel, wlr_surface *in) :
    relay(rel), surface(in)
{
    main_surface = std::make_shared<wf::scene::wlr_surface_node_t>(in, true);
    on_surface_destroy.set_callback([&] (void*)
    {
        on_map.disconnect();
        on_unmap.disconnect();
        on_destroy.disconnect();
    });

    on_map.set_callback([&] (void*) { map(); });
    on_unmap.set_callback([&] (void*) { unmap(); });
    on_commit.set_callback([&] (void*) { update_geometry(); });

    on_map.connect(&surface->events.map);
    on_unmap.connect(&surface->events.unmap);
    on_surface_destroy.connect(&surface->events.destroy);
}

std::shared_ptr<wf::text_input_v3_popup> wf::text_input_v3_popup::create(
    wf::text_input_v3_im_relay_interface_t *rel, wlr_surface *in)
{
    auto self = view_interface_t::create<wf::text_input_v3_popup>(rel, in);
    auto translation_node = std::make_shared<wf::scene::translation_node_t>();
    translation_node->set_children_list({std::make_unique<wf::scene::wlr_surface_node_t>(in, false)});
    self->surface_root_node = translation_node;
    self->set_surface_root_node(translation_node);
    self->set_role(VIEW_ROLE_DESKTOP_ENVIRONMENT);
    return self;
}

void wf::text_input_v3_popup::map()
{
    auto text_input = this->relay->find_focused_text_input_v3();
    if (!text_input)
    {
        LOGE("trying to map IM popup surface without text input.");
        return;
    }

    auto view   = wf::wl_surface_to_wayfire_view(text_input->focused_surface->resource);
    auto output = view->get_output();
    if (!output)
    {
        LOGD("trying to map input method popup with a view not on an output.");
        return;
    }

    set_output(output);

    auto target_layer = wf::scene::layer::UNMANAGED;
    wf::scene::readd_front(get_output()->node_for_layer(target_layer), get_root_node());

    priv->set_mapped_surface_contents(main_surface);
    priv->set_mapped(true);
    _is_mapped = true;
    on_commit.connect(&surface->events.commit);

    update_geometry();

    damage();
    emit_view_map();
}

void wf::text_input_v3_popup::unmap()
{
    if (!is_mapped())
    {
        return;
    }

    damage();

    priv->unset_mapped_surface_contents();

    emit_view_unmap();
    priv->set_mapped(false);
    _is_mapped = false;
    on_commit.disconnect();
}

std::string wf::text_input_v3_popup::get_app_id()
{
    return "input-method-popup";
}

std::string wf::text_input_v3_popup::get_title()
{
    return "input-method-popup";
}

void wf::text_input_v3_popup::update_geometry()
{
    auto text_input = this->relay->find_focused_text_input_v3();
    if (!text_input)
    {
        LOGI("no focused text input");
        return;
    }

    if (!is_mapped())
    {
        LOGI("input method window not mapped");
        return;
    }

    bool cursor_rect = text_input->current.features & WLR_TEXT_INPUT_V3_FEATURE_CURSOR_RECTANGLE;
    auto cursor = text_input->current.cursor_rectangle;
    int x = 0, y = 0;
    if (cursor_rect)
    {
        x = cursor.x;
        y = cursor.y + cursor.height;
    }

    auto wlr_surface = text_input->focused_surface;
    auto view = wf::wl_surface_to_wayfire_view(wlr_surface->resource);
    if (!view)
    {
        return;
    }

    damage();

    wf::pointf_t popup_offset = wf::place_popup_at(wlr_surface, surface, {x* 1.0, y * 1.0});
    x = popup_offset.x;
    y = popup_offset.y;

    auto width  = surface->current.width;
    auto height = surface->current.height;

    auto output   = view->get_output();
    auto g_output = output->get_layout_geometry();
    // make sure right edge is on screen, sliding to the left when needed,
    // but keep left edge on screen nonetheless.
    x = std::max(0, std::min(x, g_output.width - width));
    // down edge is going to be out of screen; flip upwards
    if (y + height > g_output.height)
    {
        y -= height;
        if (cursor_rect)
        {
            y -= cursor.height;
        }
    }

    // make sure top edge is on screen, sliding down and sacrificing down edge if unavoidable
    y = std::max(0, y);

    surface_root_node->set_offset({x, y});
    geometry.x     = x;
    geometry.y     = y;
    geometry.width = width;
    geometry.height = height;
    damage();
    wf::scene::update(get_surface_root_node(), wf::scene::update_flag::GEOMETRY);
}

bool wf::text_input_v3_popup::is_mapped() const
{
    return priv->wsurface != nullptr && _is_mapped;
}

wf::geometry_t wf::text_input_v3_popup::get_geometry()
{
    return geometry;
}

wf::text_input_v3_popup::~text_input_v3_popup()
{}

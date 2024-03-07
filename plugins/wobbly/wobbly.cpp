#include "wayfire/debug.hpp"
#include "wayfire/opengl.hpp"
#include "wayfire/region.hpp"
#include <memory>
#include <wayfire/plugin.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/core.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/plugins/common/util.hpp>

extern "C"
{
#include "wobbly.h"
}

#include "wayfire/plugins/wobbly/wobbly-signal.hpp"

namespace wobbly_graphics
{
namespace
{
const char *vertex_source =
    R"(
#version 100
attribute mediump vec2 position;
attribute mediump vec2 uvPosition;
varying highp vec2 uvpos;
uniform mat4 MVP;

void main() {
    gl_Position = MVP * vec4(position.xy, 0.0, 1.0);
    uvpos = uvPosition;
}
)";

const char *frag_source =
    R"(
#version 100
@builtin_ext@

varying highp vec2 uvpos;
@builtin@

void main()
{
    gl_FragColor = get_pixel(uvpos);
}
)";
}

/**
 * Enumerate the needed triangles for rendering the model
 */
void prepare_geometry(wobbly_surface *model, wf::geometry_t src_box,
    std::vector<float>& vert, std::vector<float>& uv)
{
    float x = src_box.x, y = src_box.y, w = src_box.width, h = src_box.height;
    std::vector<int> idx;

    int per_row = model->x_cells + 1;

    for (int j = 0; j < model->y_cells; j++)
    {
        for (int i = 0; i < model->x_cells; i++)
        {
            idx.push_back(i * per_row + j);
            idx.push_back((i + 1) * per_row + j + 1);
            idx.push_back(i * per_row + j + 1);

            idx.push_back(i * per_row + j);
            idx.push_back((i + 1) * per_row + j);
            idx.push_back((i + 1) * per_row + j + 1);
        }
    }

    if (!model->v || !model->uv)
    {
        for (auto id : idx)
        {
            float tile_w = w / model->x_cells;
            float tile_h = h / model->y_cells;

            int i = id / per_row;
            int j = id % per_row;

            vert.push_back(i * tile_w + x);
            vert.push_back(j * tile_h + y);

            uv.push_back(1.0f * i / model->x_cells);
            uv.push_back(1.0f - 1.0f * j / model->y_cells);
        }
    } else
    {
        for (auto i : idx)
        {
            vert.push_back(model->v[2 * i]);
            vert.push_back(model->v[2 * i + 1]);

            uv.push_back(model->uv[2 * i]);
            uv.push_back(model->uv[2 * i + 1]);
        }
    }
}

/* Requires bound opengl context */
void render_triangles(OpenGL::program_t *program, wf::texture_t tex, glm::mat4 mat, float *pos, float *uv,
    int cnt)
{
    program->use(tex.type);
    program->set_active_texture(tex);

    program->attrib_pointer("position", 2, 0, pos);
    program->attrib_pointer("uvPosition", 2, 0, uv);
    program->uniformMatrix4f("MVP", mat);

    GL_CALL(glEnable(GL_BLEND));
    GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));

    GL_CALL(glDrawArrays(GL_TRIANGLES, 0, 3 * cnt));
    GL_CALL(glDisable(GL_BLEND));

    program->deactivate();
}
}

namespace wobbly_settings
{
wf::option_wrapper_t<double> friction{"wobbly/friction"};
wf::option_wrapper_t<double> spring_k{"wobbly/spring_k"};
wf::option_wrapper_t<int> resolution{"wobbly/grid_resolution"};
}

extern "C"
{
    double wobbly_settings_get_friction()
    {
        return wf::clamp((double)wobbly_settings::friction,
            MINIMAL_FRICTION, MAXIMAL_FRICTION);
    }

    double wobbly_settings_get_spring_k()
    {
        return wf::clamp((double)wobbly_settings::spring_k,
            MINIMAL_SPRING_K, MAXIMAL_SPRING_K);
    }
}

namespace wf
{
using wobbly_model_t = std::unique_ptr<wobbly_surface>;
static const std::string wobbly_transformer_name = "wobbly";

/**
 * Different states in which wobbly can be
 */
enum ewobbly_state_t
{
    WOBBLY_STATE_FLOATING      = 0,
    WOBBLY_STATE_FREE          = 1,
    WOBBLY_STATE_GRABBED       = 2,
    WOBBLY_STATE_TILED         = 3,
    WOBBLY_STATE_TILED_GRABBED = 4,
};

/**
 * Interface representing the wobbly state.
 */
class iwobbly_state_t
{
  public:
    virtual ~iwobbly_state_t() = default;
    iwobbly_state_t(const iwobbly_state_t &) = delete;
    iwobbly_state_t(iwobbly_state_t &&) = delete;
    iwobbly_state_t& operator =(const iwobbly_state_t&) = delete;
    iwobbly_state_t& operator =(iwobbly_state_t&&) = delete;

    /** Called when the state has been updated. */
    virtual void handle_state_update_done()
    {}

    /** Called when a grab starts */
    virtual void handle_grab_start(wf::point_t grab, bool takeover)
    {}

    /** Called when the wobbly grab is moved. */
    virtual void handle_grab_move(wf::point_t grab)
    {}

    /** Query the last grab point */
    virtual wf::point_t get_grab_position() const
    {
        return {0, 0};
    }

    /**
     * Called when the wobbly grab is ended.
     * @param release_grab Whether to remove the grabbed object in the model.
     */
    virtual void handle_grab_end(bool release_grab)
    {}

    /** Called when the next frame is being prepared */
    virtual void handle_frame()
    {
        this->bounding_box = wf::view_bounding_box_up_to(view, "wobbly");
    }

    /** Called when the view wm geometry changes */
    virtual void handle_wm_geometry(const wf::geometry_t& old_wm_geometry)
    {}

    /** Called when the workspace is changed. */
    virtual void handle_workspace_change(wf::point_t old, wf::point_t cur)
    {}

    /** @return true if the wobbly animation is done. */
    virtual bool is_wobbly_done() const
    {
        return model->synced;
    }

    /** @return the current state of wobbly */
    virtual ewobbly_state_t get_wobbly_state() const = 0;

    /**
     * This isn't really meant to be used standalone, only subclasses should
     * be instantiated.
     */
    iwobbly_state_t(const wobbly_model_t& m, wayfire_toplevel_view v) :
        view(v), model(m)
    {
        bounding_box = {model->x, model->y, model->width, model->height};
    }

    /**
     * Translate the model by the given offset
     */
    virtual void translate_model(int dx, int dy)
    {
        wobbly_translate(model.get(), dx, dy);
        wobbly_add_geometry(model.get());
        bounding_box.x += dx;
        bounding_box.y += dy;
        model->x += dx;
        model->y += dy;
    }

    virtual void update_base_geometry(wf::geometry_t base)
    {
        wobbly_scale(model.get(),
            1.0 * base.width / bounding_box.width,
            1.0 * base.height / bounding_box.height);
        wobbly_translate(model.get(),
            base.x - bounding_box.x,
            base.y - bounding_box.y);
        wobbly_resize(model.get(),
            base.width,
            base.height);

        this->bounding_box = base;

        model->x     = base.x;
        model->y     = base.y;
        model->width = base.width;
        model->height = base.height;
    }

  protected:
    wayfire_toplevel_view view;
    const wobbly_model_t& model;
    wf::geometry_t bounding_box;
};

/**
 * Determines the behavior of the wobbly model when the view is grabbed,
 * for ex. when moving or resizing.
 */
class wobbly_state_grabbed_t : public iwobbly_state_t
{
  public:
    using iwobbly_state_t::iwobbly_state_t;
    virtual void handle_grab_start(wf::point_t grab, bool takeover) override
    {
        this->last_grab = {(int)grab.x, (int)grab.y};
        if (!takeover)
        {
            wobbly_grab_notify(model.get(), last_grab.x, last_grab.y);
        }
    }

    virtual wf::point_t get_grab_position() const override
    {
        return this->last_grab;
    }

    /** @return the current state of wobbly */
    virtual ewobbly_state_t get_wobbly_state() const override
    {
        return WOBBLY_STATE_GRABBED;
    }

    virtual void handle_grab_end(bool release_grab) override
    {
        if (release_grab)
        {
            wobbly_ungrab_notify(model.get());
        }
    }

    virtual void translate_model(int dx, int dy) override
    {
        iwobbly_state_t::translate_model(dx, dy);
        this->last_grab.x += dx;
        this->last_grab.y += dy;
    }

    void handle_frame() override
    {
        auto old_bbox = bounding_box;
        iwobbly_state_t::handle_frame();

        if (wf::dimensions(old_bbox) != wf::dimensions(bounding_box))
        {
            /* Directly accept new size, but keep position,
             * because it is managed by the grab. */
            wobbly_resize(model.get(), bounding_box.width, bounding_box.height);
        }
    }

  protected:
    wf::point_t last_grab;
    void handle_grab_move(wf::point_t grab) override
    {
        wobbly_move_notify(model.get(), grab.x, grab.y);
        this->last_grab = {(int)grab.x, (int)grab.y};
    }

    bool is_wobbly_done() const override
    {
        return false;
    }
};

static void wobbly_tiled_state_handle_frame(const wobbly_model_t& model,
    const wf::geometry_t& old_bbox, const wf::geometry_t& new_bbox)
{
    if (new_bbox != old_bbox)
    {
        /* Bounding box (excluding the wobbly transformer) changed, this
         * means the view got resized/moved by something outside of wobbly.
         * Adjust the geometry. */
        wobbly_force_geometry(model.get(), new_bbox.x, new_bbox.y,
            new_bbox.width, new_bbox.height);
    }
}

/**
 * Determines the behavior of the wobbly model when the view is tiled or
 * fullscreen, i.e the view keeps its geometry where it was put.
 */
class wobbly_state_tiled_t : public iwobbly_state_t
{
  public:
    using iwobbly_state_t::iwobbly_state_t;
    void handle_state_update_done() override
    {
        wobbly_force_geometry(model.get(), bounding_box.x, bounding_box.y,
            bounding_box.width, bounding_box.height);
    }

    void handle_frame() override
    {
        auto old_bbox = bounding_box;
        iwobbly_state_t::handle_frame();
        wobbly_tiled_state_handle_frame(model, old_bbox, bounding_box);
    }

    virtual ~wobbly_state_tiled_t()
    {
        wobbly_unenforce_geometry(model.get());
    }

    wobbly_state_tiled_t(const wobbly_state_tiled_t &) = delete;
    wobbly_state_tiled_t(wobbly_state_tiled_t &&) = delete;
    wobbly_state_tiled_t& operator =(const wobbly_state_tiled_t&) = delete;
    wobbly_state_tiled_t& operator =(wobbly_state_tiled_t&&) = delete;

    ewobbly_state_t get_wobbly_state() const override
    {
        return WOBBLY_STATE_TILED;
    }
};

/**
 * Determines the behavior of the wobbly model when the view is tiled or
 * fullscreen, i.e the view keeps its geometry where it was put, and it has
 * an active grab at the same time.
 *
 * This is basically a combination of tiled and grabbed.
 */
class wobbly_state_tiled_grabbed_t : public wobbly_state_grabbed_t
{
  public:
    using wobbly_state_grabbed_t::wobbly_state_grabbed_t;
    void handle_state_update_done() override
    {
        wobbly_force_geometry(model.get(), bounding_box.x, bounding_box.y,
            bounding_box.width, bounding_box.height);
    }

    void handle_frame() override
    {
        auto old_bbox = bounding_box;
        iwobbly_state_t::handle_frame();
        wobbly_tiled_state_handle_frame(model, old_bbox, bounding_box);
    }

    virtual ~wobbly_state_tiled_grabbed_t()
    {
        wobbly_unenforce_geometry(model.get());
    }

    wobbly_state_tiled_grabbed_t(const wobbly_state_tiled_grabbed_t &) = delete;
    wobbly_state_tiled_grabbed_t(wobbly_state_tiled_grabbed_t &&) = delete;
    wobbly_state_tiled_grabbed_t& operator =(const wobbly_state_tiled_grabbed_t&) =
    delete;
    wobbly_state_tiled_grabbed_t& operator =(
        wobbly_state_tiled_grabbed_t&&) = delete;

    ewobbly_state_t get_wobbly_state() const override
    {
        return WOBBLY_STATE_TILED_GRABBED;
    }
};

/**
 * Determines the behavior of the wobbly model when the view is wobblying freely
 * without being grabbed or tiled. In this state, the model dictates the
 * position of the view.
 */
class wobbly_state_floating_t : public iwobbly_state_t
{
  public:
    using iwobbly_state_t::iwobbly_state_t;

  protected:
    bool is_wobbly_done() const override
    {
        if (!model->synced)
        {
            return false;
        }

        /* Synchronize view position with the model */
        auto new_bbox = view->get_transformed_node()->get_transformer("wobbly")
            ->get_children_bounding_box();
        auto wm = view->get_geometry();

        int target_x = model->x + wm.x - new_bbox.x;
        int target_y = model->y + wm.y - new_bbox.y;
        if ((target_x != wm.x) || (target_y != wm.y))
        {
            view->move(model->x + wm.x - new_bbox.x, model->y + wm.y - new_bbox.y);
        }

        return true;
    }

    void handle_frame() override
    {
        auto new_bbox = view->get_transformed_node()->get_transformer("wobbly")
            ->get_children_bounding_box();
        update_base_geometry(new_bbox);
    }

    void handle_wm_geometry(const wf::geometry_t& old_wm) override
    {
        update_base_geometry(wf::view_bounding_box_up_to(view, "wobbly"));
    }

    void handle_workspace_change(wf::point_t old, wf::point_t cur) override
    {
        auto size  = view->get_output()->get_screen_size();
        auto delta = old - cur;
        translate_model(delta.x * size.width, delta.y * size.height);
    }

    ewobbly_state_t get_wobbly_state() const override
    {
        return WOBBLY_STATE_FLOATING;
    }
};

/**
 * Determines the behavior of wobbly when the view is not grabbed or tiled,
 * but the model should keep the true origin of the view.
 */
class wobbly_state_free_t : public iwobbly_state_t
{
  public:
    using iwobbly_state_t::iwobbly_state_t;

  protected:
    void handle_frame() override
    {
        auto old_bbox = bounding_box;
        iwobbly_state_t::handle_frame();

        if (wf::dimensions(old_bbox) != wf::dimensions(bounding_box))
        {
            wobbly_set_top_anchor(model.get(), bounding_box.x, bounding_box.y,
                bounding_box.width, bounding_box.height);
            wobbly_resize(model.get(), bounding_box.width, bounding_box.height);
        }
    }

    void handle_workspace_change(wf::point_t old, wf::point_t cur) override
    {
        auto size  = view->get_output()->get_screen_size();
        auto delta = old - cur;
        wobbly_translate(model.get(), delta.x * size.width, delta.y * size.height);
    }

    ewobbly_state_t get_wobbly_state() const override
    {
        return WOBBLY_STATE_FREE;
    }
};
}

class wobbly_transformer_node_t : public wf::scene::transformer_base_node_t
{
  public:
    wobbly_transformer_node_t(wayfire_toplevel_view view,
        OpenGL::program_t *wobbly_prog) : transformer_base_node_t(false)
    {
        this->view = view;
        this->wobbly_program = wobbly_prog;
        init_model();
        last_frame = wf::get_current_time();
        view->get_output()->connect(&on_workspace_changed);

        view->connect(&on_view_unmap);
        view->connect(&on_view_tiled);
        view->connect(&on_view_fullscreen);
        view->connect(&view_output_changed);
        view->connect(&on_view_geometry_changed);

        /* Set to free state initially but then look for the correct state */
        this->state = std::make_unique<wf::wobbly_state_free_t>(model, view);
        update_wobbly_state(false, {0, 0}, false);
    }

    ~wobbly_transformer_node_t()
    {
        state = nullptr;
        wobbly_fini(model.get());
    }

    std::string stringify() const override
    {
        return "wobbly";
    }

    wf::geometry_t get_bounding_box() override
    {
        auto box = wobbly_boundingbox(model.get());

        wlr_box result;
        result.x     = box.tlx;
        result.y     = box.tly;
        result.width = std::ceil(box.brx - box.tlx);
        result.height = std::ceil(box.bry - box.tly);
        return result;
    }

    void gen_render_instances(
        std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push_damage, wf::output_t *shown_on) override;

    std::unique_ptr<wobbly_surface> model;

    void destroy_self()
    {
        view->get_transformed_node()->rem_transformer("wobbly");
    }

    OpenGL::program_t *wobbly_program;

  private:
    wayfire_toplevel_view view;

    wf::signal::connection_t<wf::view_unmapped_signal> on_view_unmap = [=] (wf::view_unmapped_signal*)
    {
        destroy_self();
    };

    wf::signal::connection_t<wf::view_tiled_signal> on_view_tiled = [=] (wf::view_tiled_signal *ev)
    {
        update_wobbly_state(false, {0, 0}, false);
    };

    wf::signal::connection_t<wf::view_fullscreen_signal> on_view_fullscreen =
        [=] (wf::view_fullscreen_signal *ev)
    {
        update_wobbly_state(false, {0, 0}, false);
    };

    wf::signal::connection_t<wf::view_geometry_changed_signal> on_view_geometry_changed =
        [=] (wf::view_geometry_changed_signal *ev)
    {
        state->handle_wm_geometry(ev->old_geometry);
    };

    wf::signal::connection_t<wf::workspace_changed_signal> on_workspace_changed =
        [=] (wf::workspace_changed_signal *ev)
    {
        state->handle_workspace_change(ev->old_viewport, ev->new_viewport);
    };

    wf::signal::connection_t<wf::view_set_output_signal> view_output_changed =
        [=] (wf::view_set_output_signal *ev)
    {
        /* Wobbly is active only when there's already been an output */
        wf::dassert(ev->output != nullptr, "wobbly cannot be active on nullptr output!");
        if (!view->get_output())
        {
            // Destructor won't be able to disconnect bc view output is invalid
            return destroy_self();
        }

        /* Translate wobbly when its output changes */
        auto old_geometry = ev->output->get_layout_geometry();
        auto new_geometry = view->get_output()->get_layout_geometry();
        state->translate_model(old_geometry.x - new_geometry.x, old_geometry.y - new_geometry.y);

        on_workspace_changed.disconnect();
        view->get_output()->connect(&on_workspace_changed);
    };

    std::unique_ptr<wf::iwobbly_state_t> state;
    uint32_t last_frame;
    bool force_tile = false;

    void init_model()
    {
        model = std::make_unique<wobbly_surface>();
        auto g = view->get_bounding_box();

        model->x     = g.x;
        model->y     = g.y;
        model->width = g.width;
        model->height = g.height;

        model->grabbed = 0;
        model->synced  = 1;

        model->x_cells = wobbly_settings::resolution;
        model->y_cells = wobbly_settings::resolution;

        model->v  = NULL;
        model->uv = NULL;
        wobbly_init(model.get());
    }

  public:
    void update_model()
    {
        view->damage();

        /* It is possible that the wobbly state needs to adjust view geometry.
         * We do not want it to get feedback from itself */
        on_view_geometry_changed.disconnect();
        state->handle_frame();
        view->connect(&on_view_geometry_changed);

        /* Update all the wobbly model */
        auto now = wf::get_current_time();

        if (now > last_frame)
        {
            view->get_transformed_node()->begin_transform_update();
            wobbly_prepare_paint(model.get(), now - last_frame);
            /* Update wobbly geometry */
            last_frame = now;
            wobbly_add_geometry(model.get());
            wobbly_done_paint(model.get());
            view->get_transformed_node()->end_transform_update();
        }

        if (state->is_wobbly_done())
        {
            destroy_self();
        }
    }

    /**
     * Update the current wobbly state based on:
     * 1. View state (tiled & fullscreen)
     * 2. Current wobbly state (grabbed or not)
     * 3. Whether we are starting a grab, or ending a grab.
     *
     * @param start_grab Whether to start a new grab at @grab
     * @param grab The position of the starting grab.
     * @param end_grab Whether to end an existing grab.
     */
    void update_wobbly_state(bool start_grab, wf::point_t grab, bool end_grab)
    {
        bool was_grabbed =
            (state->get_wobbly_state() == wf::WOBBLY_STATE_GRABBED ||
                state->get_wobbly_state() == wf::WOBBLY_STATE_TILED_GRABBED);
        bool grabbed = (start_grab || was_grabbed) && !end_grab;

        bool tiled = false;
        if (grabbed)
        {
            // If the view is grabbed, the grabbing plugin says whether to tile
            // or not
            tiled = force_tile;
        } else
        {
            tiled = (force_tile || view->pending_tiled_edges()) || view->pending_fullscreen();
        }

        uint32_t next_state_mask = 0;
        if (tiled && grabbed)
        {
            next_state_mask = wf::WOBBLY_STATE_TILED_GRABBED;
        } else if (tiled)
        {
            next_state_mask = wf::WOBBLY_STATE_TILED;
        } else if (grabbed)
        {
            next_state_mask = wf::WOBBLY_STATE_GRABBED;
        } else if (was_grabbed ||
                   (state->get_wobbly_state() == wf::WOBBLY_STATE_FLOATING))
        {
            /* If previously grabbed, we can let the view float freely */
            next_state_mask = wf::WOBBLY_STATE_FLOATING;
        } else
        {
            /* Otherwise, we need to keep the position */
            next_state_mask = wf::WOBBLY_STATE_FREE;
        }

        if (next_state_mask == state->get_wobbly_state())
        {
            return;
        }

        std::unique_ptr<wf::iwobbly_state_t> next_state;
        switch (next_state_mask)
        {
          case wf::WOBBLY_STATE_FREE:
            next_state = std::make_unique<
                wf::wobbly_state_free_t>(model, view);
            break;

          case wf::WOBBLY_STATE_FLOATING:
            next_state = std::make_unique<
                wf::wobbly_state_floating_t>(model, view);
            break;

          case wf::WOBBLY_STATE_TILED:
            next_state = std::make_unique<
                wf::wobbly_state_tiled_t>(model, view);
            break;

          case wf::WOBBLY_STATE_GRABBED:
            next_state = std::make_unique<
                wf::wobbly_state_grabbed_t>(model, view);
            break;

          case wf::WOBBLY_STATE_TILED_GRABBED:
            next_state = std::make_unique<
                wf::wobbly_state_tiled_grabbed_t>(model, view);
            break;

          default:
            /* Not reached except by a bug */
            assert(false);
        }

        if (was_grabbed)
        {
            this->state->handle_grab_end(end_grab);
        }

        if (grabbed)
        {
            if (was_grabbed)
            {
                grab = this->state->get_grab_position();
            }

            next_state->handle_grab_start(grab, was_grabbed);
        }

        /* New state has been set up */
        this->state = std::move(next_state);
        this->state->handle_state_update_done();
    }

  public:
    void start_grab(wf::point_t grab)
    {
        update_wobbly_state(true, grab, false);
    }

    void move(wf::point_t point)
    {
        state->handle_grab_move(point);
    }

    void translate(wf::point_t delta)
    {
        state->translate_model(delta.x, delta.y);
    }

    void end_grab()
    {
        update_wobbly_state(false, {0, 0}, true);
    }

    void wobble()
    {
        wobbly_slight_wobble(model.get());
        model->synced = 0;
    }

    void update_base_geometry(wf::geometry_t g)
    {
        state->update_base_geometry(g);
    }

    void set_force_tile(bool force_tile)
    {
        this->force_tile = force_tile;
        update_wobbly_state(false, {0, 0}, false);
    }
};

class wobbly_render_instance_t :
    public wf::scene::transformer_render_instance_t<wobbly_transformer_node_t>
{
    wf::output_t *wo = nullptr;
    wf::effect_hook_t pre_hook;

  public:
    wobbly_render_instance_t(wobbly_transformer_node_t *self, wf::scene::damage_callback push_damage,
        wf::output_t *shown_on) : transformer_render_instance_t(self, push_damage, shown_on)
    {
        if (shown_on)
        {
            wo = shown_on;
            pre_hook = [=] () { self->update_model(); };
            wo->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);
        }
    }

    ~wobbly_render_instance_t()
    {
        if (wo)
        {
            wo->render->rem_effect(&pre_hook);
        }
    }

    void transform_damage_region(wf::region_t& damage) override
    {
        damage |= self->get_bounding_box();
    }

    void render(const wf::render_target_t& target_fb,
        const wf::region_t& damage) override
    {
        std::vector<float> vert, uv;
        auto subbox = self->get_children_bounding_box();

        wobbly_graphics::prepare_geometry(self->model.get(), subbox, vert, uv);
        auto tex = get_texture(target_fb.scale);
        OpenGL::render_begin(target_fb);
        for (auto& box : damage)
        {
            target_fb.logic_scissor(wlr_box_from_pixman_box(box));
            wobbly_graphics::render_triangles(self->wobbly_program, tex,
                target_fb.get_orthographic_projection(),
                vert.data(), uv.data(),
                self->model->x_cells * self->model->y_cells * 2);
        }

        OpenGL::render_end();
    }
};

void wobbly_transformer_node_t::gen_render_instances(
    std::vector<wf::scene::render_instance_uptr>& instances,
    wf::scene::damage_callback push_damage, wf::output_t *shown_on)
{
    instances.push_back(std::make_unique<wobbly_render_instance_t>(
        this, push_damage, shown_on));
}

class wayfire_wobbly : public wf::plugin_interface_t
{
    wf::signal::connection_t<wobbly_signal> wobbly_changed = [=] (wobbly_signal *ev)
    {
        adjust_wobbly(ev);
    };

  public:
    void init() override
    {
        wf::get_core().connect(&wobbly_changed);
        OpenGL::render_begin();
        program.compile(wobbly_graphics::vertex_source, wobbly_graphics::frag_source);
        OpenGL::render_end();
    }

    void adjust_wobbly(wobbly_signal *data)
    {
        auto tr_manager = data->view->get_transformed_node();
        if ((data->events & (WOBBLY_EVENT_GRAB | WOBBLY_EVENT_ACTIVATE)) &&
            !tr_manager->get_transformer<wobbly_transformer_node_t>("wobbly"))
        {
            tr_manager->add_transformer(
                std::make_shared<wobbly_transformer_node_t>(data->view, &program),
                wf::TRANSFORMER_HIGHLEVEL, "wobbly");
        }

        auto wobbly =
            tr_manager->get_transformer<wobbly_transformer_node_t>("wobbly");
        if (!wobbly)
        {
            return;
        }

        if (data->events & WOBBLY_EVENT_ACTIVATE)
        {
            wobbly->wobble();
        }

        if (data->events & WOBBLY_EVENT_GRAB)
        {
            wobbly->start_grab(data->pos);
        }

        if (data->events & WOBBLY_EVENT_MOVE)
        {
            wobbly->move(data->pos);
        }

        if (data->events & WOBBLY_EVENT_TRANSLATE)
        {
            wobbly->translate(data->pos);
        }

        if (data->events & WOBBLY_EVENT_END)
        {
            wobbly->end_grab();
        }

        if (data->events & WOBBLY_EVENT_FORCE_TILE)
        {
            wobbly->set_force_tile(true);
        }

        if (data->events & WOBBLY_EVENT_UNTILE)
        {
            wobbly->set_force_tile(false);
        }

        if (data->events & WOBBLY_EVENT_SCALE)
        {
            wobbly->update_base_geometry(data->geometry);
        }
    }

    void fini() override
    {
        for (auto& view : wf::get_core().get_all_views())
        {
            auto wobbly = view->get_transformed_node()->get_transformer<wobbly_transformer_node_t>();
            if (wobbly)
            {
                wobbly->destroy_self();
            }
        }

        OpenGL::render_begin();
        program.free_resources();
        OpenGL::render_end();
    }

  private:
    OpenGL::program_t program;
};

DECLARE_WAYFIRE_PLUGIN(wayfire_wobbly);

#include <wayfire/per-output-plugin.hpp>
#include <memory>
#include <wayfire/plugin.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/workspace-stream.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/plugins/common/input-grab.hpp>
#include "wayfire/plugins/ipc/ipc-activator.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <wayfire/img.hpp>

#include "cube.hpp"
#include "simple-background.hpp"
#include "skydome.hpp"
#include "cubemap.hpp"
#include "cube-control-signal.hpp"
#include "wayfire/region.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-definitions.hpp"

#define Z_OFFSET_NEAR 0.89567f
#define Z_OFFSET_FAR  2.00000f

#define ZOOM_MAX 10.0f
#define ZOOM_MIN 0.1f

#ifdef USE_GLES32
    #include <GLES3/gl32.h>
#endif

#include "shaders.tpp"
#include "windows3d-shaders.tpp"
#include "windows3d.hpp"
#include "shaders-3-2.tpp"

class wayfire_cube : public wf::per_output_plugin_instance_t, public wf::pointer_interaction_t
{
    class cube_render_node_t : public wf::scene::node_t
    {
        class cube_render_instance_t : public wf::scene::render_instance_t
        {
            std::shared_ptr<cube_render_node_t> self;
            wf::scene::damage_callback push_damage;

            std::vector<std::vector<wf::scene::render_instance_uptr>> ws_instances;
            std::vector<wf::regionf_t> ws_damage;
            std::vector<wf::auxilliary_buffer_t> framebuffers;
            /* 3D windows: per face, its windows (empty when off) */
            cube3d::face_windows_t face_windows;

            wf::signal::connection_t<wf::scene::node_damage_signal> on_cube_damage =
                [=] (wf::scene::node_damage_signal *ev)
            {
                push_damage(ev->region);
            };

          public:
            cube_render_instance_t(cube_render_node_t *self, wf::scene::damage_callback push_damage)
            {
                this->self = std::dynamic_pointer_cast<cube_render_node_t>(self->shared_from_this());
                this->push_damage = push_damage;
                self->connect(&on_cube_damage);

                ws_damage.resize(self->workspaces.size());
                framebuffers.resize(self->workspaces.size());
                ws_instances.resize(self->workspaces.size());
                for (int i = 0; i < (int)self->workspaces.size(); i++)
                {
                    auto push_damage_child = [=] (const wf::regionf_t& damage)
                    {
                        ws_damage[i] |= damage;
                        push_damage(self->get_bounding_box());
                    };

                    self->workspaces[i]->gen_render_instances(ws_instances[i],
                        push_damage_child, self->cube->output);

                    ws_damage[i] |= self->workspaces[i]->get_bounding_box();
                }

                face_windows.resize(self->workspaces.size());
                for (int i = 0; i < (int)self->windows.size(); i++)
                {
                    const int n = self->windows[i].size();
                    for (int k = 0; k < n; k++)
                    {
                        auto node = self->windows[i][k].lock();
                        if (!node)
                        {
                            continue;
                        }

                        auto layer = std::make_unique<cube3d::window_layer_t>();
                        layer->node  = node;
                        layer->level = float(n - k) / n; /* topmost (k = 0) = 1 */
                        node->gen_render_instances(layer->instances,
                            [=] (const wf::regionf_t&) { push_damage(self->get_bounding_box()); },
                            self->cube->output);
                        face_windows[i].push_back(std::move(layer));
                    }
                }
            }

            ~cube_render_instance_t()
            {}

            void schedule_instructions(
                std::vector<wf::scene::render_instruction_t>& instructions,
                const wf::render_target_t& target, wf::regionf_t& damage) override
            {
                instructions.push_back(wf::scene::render_instruction_t{
                    .instance = this,
                    .target   = target.translated(-wf::origin(self->get_bounding_box())),
                    .damage   = damage & self->get_bounding_box(),
                });

                auto bbox = self->get_bounding_box();

                damage ^= bbox;

                const bool is_hdr = self->cube->output && self->cube->output->is_hdr();

                for (int i = 0; i < (int)ws_instances.size(); i++)
                {
                    const float scale = self->cube->output->handle->scale;
                    auto bbox = self->workspaces[i]->get_bounding_box();
                    framebuffers[i].allocate(wf::dimensions(bbox), scale,
                        wf::buffer_allocation_hints_t{.hdr_linear = is_hdr});

                    wf::render_target_t target{framebuffers[i]};
                    target.geometry = self->workspaces[i]->get_bounding_box();
                    target.scale    = self->cube->output->handle->scale;

                    wf::render_pass_params_t params;
                    params.instances = &ws_instances[i];
                    params.damage    = ws_damage[i];
                    params.reference_output = self->cube->output;
                    params.target = target;
                    params.flags  = wf::RPASS_CLEAR_BACKGROUND | wf::RPASS_EMIT_SIGNALS;

                    wf::render_pass_t::run(params);
                    ws_damage[i].clear();
                }

                render_windows(is_hdr);
            }

            /* 3D windows: render every window of every face into its own
             * buffer -- the part of it which lies on that face's workspace */
            void render_windows(bool is_hdr)
            {
                auto output = self->cube->output;
                auto og     = output->get_relative_geometry();
                auto cws    = output->wset()->get_current_workspace();
                const float scale = output->handle->scale;
                for (int i = 0; i < (int)face_windows.size(); i++)
                {
                    wf::geometry_t ws_rect = {(i - cws.x) * og.width,
                        (self->row - cws.y) * og.height, og.width, og.height};
                    for (auto& layer : face_windows[i])
                    {
                        layer->rect = {0, 0, 0, 0};
                        auto node = layer->node.lock();
                        if (!node || !node->is_enabled())
                        {
                            continue;
                        }

                        auto rect = wf::geometry_intersection(node->get_bounding_box(), ws_rect);
                        if ((rect.width <= 0) || (rect.height <= 0))
                        {
                            continue;
                        }

                        layer->buffer.allocate(wf::dimensions(rect), scale,
                            wf::buffer_allocation_hints_t{.hdr_linear = is_hdr});
                        wf::render_target_t target{layer->buffer};
                        target.geometry = rect;
                        target.scale    = scale;

                        wf::render_pass_params_t params;
                        params.instances = &layer->instances;
                        params.damage    = rect;
                        params.reference_output = output;
                        params.target = target;
                        params.background_color = {0, 0, 0, 0}; /* transparent */
                        params.flags = wf::RPASS_CLEAR_BACKGROUND | wf::RPASS_EMIT_SIGNALS;
                        wf::render_pass_t::run(params);

                        layer->rect = {rect.x - ws_rect.x, rect.y - ws_rect.y,
                            rect.width, rect.height};

                        /* thickness edges follow the window itself, not its
                         * shadow (part of the bounding box) */
                        layer->slab = {0, 0, 0, 0};
                        if (auto view = wf::toplevel_cast(wf::node_to_view(node)))
                        {
                            auto s = wf::geometry_intersection(view->get_geometry(), ws_rect);
                            if ((s.width > 0) && (s.height > 0))
                            {
                                layer->slab = {s.x - ws_rect.x, s.y - ws_rect.y, s.width, s.height};
                            }
                        }
                    }
                }
            }

            void render(const wf::scene::render_instruction_t& data) override
            {
                self->cube->render(data, framebuffers, &face_windows);
            }

            void compute_visibility(wf::output_t *output, wf::regionf_t& visible) override
            {
                for (int i = 0; i < (int)self->workspaces.size(); i++)
                {
                    wf::regionf_t ws_region = self->workspaces[i]->get_bounding_box();
                    for (auto& ch : this->ws_instances[i])
                    {
                        ch->compute_visibility(output, ws_region);
                    }
                }
            }
        };

      public:
        cube_render_node_t(wayfire_cube *cube) : node_t(false)
        {
            this->cube = cube;
            auto w = cube->output->wset()->get_workspace_grid_size().width;
            auto y = cube->output->wset()->get_current_workspace().y;
            this->row = y;
            for (int i = 0; i < w; i++)
            {
                if (!cube->windows_3d)
                {
                    auto node = std::make_shared<wf::workspace_stream_node_t>(cube->output,
                        wf::point_t{i, y});
                    workspaces.push_back(node);
                    continue;
                }

                /* 3D windows: the face shows the desktop without its windows,
                 * the windows are drawn separately */
                auto wins = cube3d::workspace_windows(cube->output, {i, y});
                std::set<wf::scene::node_t*> skip;
                windows.emplace_back();
                for (auto& win : wins)
                {
                    skip.insert(win.get());
                    windows.back().push_back(win);
                }

                workspaces.push_back(std::make_shared<cube3d::base_stream_node_t>(
                    cube->output, wf::point_t{i, y}, skip));
            }
        }

        virtual void gen_render_instances(
            std::vector<wf::scene::render_instance_uptr>& instances,
            wf::scene::damage_callback push_damage, wf::output_t *shown_on)
        {
            if (shown_on != this->cube->output)
            {
                return;
            }

            instances.push_back(std::make_unique<cube_render_instance_t>(
                this, push_damage));
        }

        wf::geometry_t get_bounding_box()
        {
            return cube->output->get_layout_geometry();
        }

      private:
        std::vector<std::shared_ptr<wf::scene::node_t>> workspaces;
        /* 3D windows: per face, the window nodes, topmost first */
        std::vector<std::vector<std::weak_ptr<wf::scene::node_t>>> windows;
        int row = 0;
        wayfire_cube *cube;
    };

    std::unique_ptr<wf::input_grab_t> input_grab;
    std::shared_ptr<cube_render_node_t> render_node;

    wf::option_wrapper_t<double> XVelocity{"cube/speed_spin_horiz"},
    YVelocity{"cube/speed_spin_vert"}, ZVelocity{"cube/speed_zoom"};
    wf::option_wrapper_t<double> zoom_opt{"cube/zoom"};

    /* the Z camera distance so that (-1, 1) is mapped to the whole screen
     * for the given FOV */
    float identity_z_offset;

    OpenGL::program_t program;

    wf_cube_animation_attribs animation;
    wf::option_wrapper_t<bool> use_light{"cube/light"};
    wf::option_wrapper_t<int> use_deform{"cube/deform"};

    /* 3D windows: windows lifted off the faces */
    wf::option_wrapper_t<bool> windows_3d{"cube/windows_3d"};
    wf::option_wrapper_t<double> windows_3d_depth{"cube/windows_3d_depth"};
    wf::option_wrapper_t<double> windows_3d_thickness{"cube/windows_3d_thickness"};
    wf::option_wrapper_t<wf::color_t> windows_3d_edge_color{"cube/windows_3d_edge_color"};
    wf::option_wrapper_t<bool> windows_3d_edge_from_window{"cube/windows_3d_edge_from_window"};
    OpenGL::program_t window_program, edge_program;
    std::string last_background_mode;
    std::unique_ptr<wf_cube_background_base> background;

    /* Last pointer position seen while grabbed, used to turn the absolute
     * positions reported by tablets and touchscreens into deltas. Empty
     * until the first motion event, so that the first event does not cause
     * a jump. */
    std::optional<wf::pointf_t> last_pointer_position;

    wf::option_wrapper_t<std::string> background_mode{"cube/background_mode"};

    void reload_background()
    {
        if (last_background_mode == (std::string)background_mode)
        {
            return;
        }

        last_background_mode = background_mode;

        if (last_background_mode == "simple")
        {
            background = std::make_unique<wf_cube_simple_background>();
        } else if (last_background_mode == "skydome")
        {
            background = std::make_unique<wf_cube_background_skydome>(output);
        } else if (last_background_mode == "cubemap")
        {
            background = std::make_unique<wf_cube_background_cubemap>();
        } else
        {
            LOGE("cube: Unrecognized background mode %s. Using default \"simple\"",
                last_background_mode.c_str());
            background = std::make_unique<wf_cube_simple_background>();
        }
    }

    bool tessellation_support;

    int get_num_faces()
    {
        return output->wset()->get_workspace_grid_size().width;
    }

    wf::plugin_activation_data_t grab_interface{
        .name = "cube",
        .capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR,
        .cancel = [=] () { deactivate(); },
    };

  public:
    void init() override
    {
        input_grab = std::make_unique<wf::input_grab_t>("cube", output, nullptr, this, nullptr);
        input_grab->set_wants_raw_input(true);

        animation.cube_animation.offset_y.set(0, 0);
        animation.cube_animation.offset_z.set(0, 0);
        animation.cube_animation.rotation.set(0, 0);
        animation.cube_animation.zoom.set(1, 1);
        animation.cube_animation.ease_deformation.set(0, 0);

        animation.cube_animation.start();

        reload_background();

        output->connect(&on_cube_control);
        wf::gles::run_in_context([&]
        {
            load_program();
        });
    }

    void handle_pointer_button(const wlr_pointer_button_event& event) override
    {
        if (event.state == WL_POINTER_BUTTON_STATE_RELEASED)
        {
            input_ungrabbed();
        }
    }

    void handle_pointer_axis(const wlr_pointer_axis_event& event) override
    {
        if (event.orientation == WL_POINTER_AXIS_VERTICAL_SCROLL)
        {
            pointer_scrolled(event.delta);
        }
    }

    /**
     * Rotate the cube when dragging with an absolute pointing device.
     *
     * Relative devices are handled by on_motion_event below, which reads the
     * raw deltas and then zeroes them so that the pointer stays locked in
     * place while the cube is grabbed. Absolute devices (drawing tablets,
     * touchscreens used as pointers, and the virtual tablets that QEMU,
     * VirtualBox and VMware expose to guests) never produce those deltas, so
     * without this handler the cube opens but cannot be rotated at all.
     *
     * The pointer cannot be locked for absolute devices, so the rotation
     * stops once the pointer reaches the edge of the output.
     */
    void handle_pointer_motion(wf::pointf_t pointer_position, uint32_t time_ms) override
    {
        if (last_pointer_position)
        {
            rotate_cube(pointer_position.x - last_pointer_position->x,
                pointer_position.y - last_pointer_position->y);
        }

        last_pointer_position = pointer_position;
    }

    void load_program()
    {
#ifdef USE_GLES32
        std::string ext_string(reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS)));
        tessellation_support = ext_string.find(std::string("GL_EXT_tessellation_shader")) !=
            std::string::npos;
#else
        tessellation_support = false;
#endif

        if (!tessellation_support)
        {
            program.set_simple(OpenGL::compile_program(cube_vertex_2_0, cube_fragment_2_0));
        } else
        {
#ifdef USE_GLES32
            auto id = GL_CALL(glCreateProgram());
            GLuint vss, fss, tcs, tes, gss;

            vss = OpenGL::compile_shader(cube_vertex_3_2, GL_VERTEX_SHADER);
            fss = OpenGL::compile_shader(cube_fragment_3_2, GL_FRAGMENT_SHADER);
            tcs = OpenGL::compile_shader(cube_tcs_3_2, GL_TESS_CONTROL_SHADER);
            tes = OpenGL::compile_shader(cube_tes_3_2, GL_TESS_EVALUATION_SHADER);
            gss = OpenGL::compile_shader(cube_geometry_3_2, GL_GEOMETRY_SHADER);

            GL_CALL(glAttachShader(id, vss));
            GL_CALL(glAttachShader(id, tcs));
            GL_CALL(glAttachShader(id, tes));
            GL_CALL(glAttachShader(id, gss));
            GL_CALL(glAttachShader(id, fss));

            GL_CALL(glLinkProgram(id));
            GL_CALL(glUseProgram(id));

            GL_CALL(glDeleteShader(vss));
            GL_CALL(glDeleteShader(fss));
            GL_CALL(glDeleteShader(tcs));
            GL_CALL(glDeleteShader(tes));
            GL_CALL(glDeleteShader(gss));
            program.set_simple(id);
#endif
        }

        window_program.set_simple(OpenGL::compile_program(cube_vertex_2_0, cube_window_fragment));
        edge_program.set_simple(OpenGL::compile_program(cube_edge_vertex, cube_edge_fragment));
        animation.projection = glm::perspective(45.0f, 1.f, 0.1f, 100.f);
    }

    wf::signal::connection_t<cube_control_signal> on_cube_control = [=] (cube_control_signal *d)
    {
        rotate_and_zoom_cube(d->angle, d->zoom, d->ease, d->last_frame);
        d->carried_out = true;
    };

    void rotate_and_zoom_cube(double angle, double zoom, double ease,
        bool last_frame)
    {
        if (last_frame)
        {
            deactivate();

            return;
        }

        if (!activate())
        {
            return;
        }

        float offset_z = identity_z_offset + Z_OFFSET_NEAR;

        animation.cube_animation.rotation.set(angle, angle);
        animation.cube_animation.zoom.set(zoom, zoom);
        animation.cube_animation.ease_deformation.set(ease, ease);

        animation.cube_animation.offset_y.set(0, 0);
        animation.cube_animation.offset_z.set(offset_z, offset_z);

        animation.cube_animation.start();
        update_view_matrix();
        output->render->schedule_redraw();
    }

    /* Tries to initialize renderer, activate plugin, etc. */
    bool activate()
    {
        if (output->is_plugin_active(grab_interface.name))
        {
            return true;
        }

        if (!output->activate_plugin(&grab_interface))
        {
            return false;
        }

        last_pointer_position = {};
        wf::get_core().connect(&on_motion_event);

        render_node = std::make_shared<cube_render_node_t>(this);
        wf::scene::add_front(wf::get_core().scene(), render_node);
        output->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);
        output->render->set_require_depth_buffer(true);

        wf::get_core().hide_cursor();
        input_grab->grab_input(wf::scene::layer::OVERLAY);

        auto wsize = output->wset()->get_workspace_grid_size();
        animation.side_angle = 2 * M_PI / float(wsize.width);
        identity_z_offset    = 0.5 / std::tan(animation.side_angle / 2);
        if (wsize.width == 1)
        {
            // tan(M_PI) is 0, so identity_z_offset is invalid
            identity_z_offset = 0.0f;
        }

        reload_background();
        animation.cube_animation.offset_z.set(identity_z_offset + Z_OFFSET_NEAR,
            identity_z_offset + Z_OFFSET_NEAR);
        return true;
    }

    int calculate_viewport_dx_from_rotation()
    {
        float dx = -animation.cube_animation.rotation / animation.side_angle;

        return std::floor(dx + 0.5);
    }

    /* Disable custom rendering and deactivate plugin */
    void deactivate()
    {
        if (!output->is_plugin_active(grab_interface.name))
        {
            return;
        }

        wf::scene::remove_child(render_node);
        output->render->damage_whole();

        render_node = nullptr;
        output->render->rem_effect(&pre_hook);
        output->render->set_require_depth_buffer(false);

        input_grab->ungrab_input();
        output->deactivate_plugin(&grab_interface);
        wf::get_core().unhide_cursor();
        on_motion_event.disconnect();
        last_pointer_position = {};

        /* Figure out how much we have rotated and switch workspace */
        int size = get_num_faces();
        int dvx  = calculate_viewport_dx_from_rotation();

        auto cws = output->wset()->get_current_workspace();
        int nvx  = (cws.x + (dvx % size) + size) % size;
        output->wset()->set_workspace({nvx, cws.y});

        /* We are finished with rotation, make sure the next time cube is used
         * it is properly reset */
        animation.cube_animation.rotation.set(0, 0);
    }

    /* Sets attributes target to such values that the cube effect isn't visible,
     * i.e towards the starting(or ending) position
     *
     * It doesn't change rotation because that is different in different cases -
     * for example when moved by the keyboard or with a button grab */
    void reset_attribs()
    {
        animation.cube_animation.zoom.restart_with_end(1.0);
        animation.cube_animation.offset_z.restart_with_end(
            identity_z_offset + Z_OFFSET_NEAR);
        animation.cube_animation.offset_y.restart_with_end(0);
        animation.cube_animation.ease_deformation.restart_with_end(0);
    }

    /* Start moving to a workspace to the left/right using the keyboard */
    bool move_vp(int dir)
    {
        if (!activate())
        {
            return false;
        }

        /* After the rotation is done, we want to exit cube and focus the target
         * workspace */
        animation.in_exit = true;

        /* Set up rotation target to the next workspace in the given direction,
         * and reset other attribs */
        reset_attribs();
        animation.cube_animation.rotation.restart_with_end(
            animation.cube_animation.rotation.end - dir * animation.side_angle);

        animation.cube_animation.start();
        update_view_matrix();
        output->render->schedule_redraw();

        return true;
    }

    /* Initiate with an button grab. */
    bool input_grabbed()
    {
        if (!activate())
        {
            return false;
        }

        /* Rotations, offset_y and zoom stay as they are now, as they have been
         * grabbed.
         * offset_z changes to the default one.
         *
         * We also need to make sure the cube gets deformed */
        animation.in_exit = false;
        float current_rotation = animation.cube_animation.rotation;
        float current_offset_y = animation.cube_animation.offset_y;
        float current_zoom     = animation.cube_animation.zoom;

        animation.cube_animation.rotation.set(current_rotation, current_rotation);
        animation.cube_animation.offset_y.set(current_offset_y, current_offset_y);
        animation.cube_animation.offset_z.restart_with_end(
            zoom_opt + identity_z_offset + Z_OFFSET_NEAR);

        animation.cube_animation.zoom.set(current_zoom, current_zoom);
        animation.cube_animation.ease_deformation.restart_with_end(1);

        animation.cube_animation.start();

        update_view_matrix();
        output->render->schedule_redraw();

        // Let the button go to the input grab
        return false;
    }

    /* Mouse grab was released */
    void input_ungrabbed()
    {
        animation.in_exit = true;

        /* Rotate cube so that selected workspace aligns with the output */
        float current_rotation = animation.cube_animation.rotation;
        int dvx = calculate_viewport_dx_from_rotation();
        animation.cube_animation.rotation.set(current_rotation,
            -dvx * animation.side_angle);
        /* And reset other attributes, again to align the workspace with the output
         * */
        reset_attribs();

        animation.cube_animation.start();

        update_view_matrix();
        output->render->schedule_redraw();
    }

    /* Update the view matrix used in the next frame */
    void update_view_matrix()
    {
        auto zoom_translate = glm::translate(glm::mat4(1.f),
            glm::vec3(0.f, 0.f, -animation.cube_animation.offset_z));

        auto rotation = glm::rotate(glm::mat4(1.0),
            (float)animation.cube_animation.offset_y,
            glm::vec3(1., 0., 0.));

        auto view = glm::lookAt(glm::vec3(0., 0., 0.),
            glm::vec3(0., 0., -animation.cube_animation.offset_z),
            glm::vec3(0., 1., 0.));

        animation.view = zoom_translate * rotation * view;
    }

    glm::mat4 output_transform(const wf::render_target_t& target)
    {
        auto scale = glm::scale(glm::mat4(1.0), {1, -1, 1});
        return wf::gles::render_target_gl_to_framebuffer(target) * scale;
    }

    glm::mat4 calculate_vp_matrix(const wf::render_target_t& dest)
    {
        float zoom_factor = animation.cube_animation.zoom;
        auto scale_matrix = glm::scale(glm::mat4(1.0),
            glm::vec3(1. / zoom_factor, 1. / zoom_factor, 1. / zoom_factor));

        return output_transform(dest) * animation.projection * animation.view * scale_matrix;
    }

    /* Calculate the base model matrix for the i-th side of the cube */
    glm::mat4 calculate_model_matrix(int i)
    {
        const float angle =
            i * animation.side_angle + animation.cube_animation.rotation;
        auto rotation = glm::rotate(glm::mat4(1.0), angle, glm::vec3(0, 1, 0));

        double additional_z = 0.0;
        // Special case: 2 faces
        // In this case, we need to make sure that the two faces are just
        // slightly moved away from each other, to avoid artifacts which can
        // happen if both sides are touching.
        if (get_num_faces() == 2)
        {
            additional_z = 1e-3;
        }

        auto translation = glm::translate(glm::mat4(1.0),
            glm::vec3(0, 0, identity_z_offset + additional_z));

        return rotation * translation;
    }

    /* Render the sides of the cube, using the given culling mode - cw or ccw */
    void render_cube(GLuint front_face, std::vector<wf::auxilliary_buffer_t>& buffers)
    {
        GL_CALL(glFrontFace(front_face));
        static const GLuint indexData[] = {0, 1, 2, 0, 2, 3};

        auto cws = output->wset()->get_current_workspace();
        for (int i = 0; i < get_num_faces(); i++)
        {
            int index = (cws.x + i) % get_num_faces();
            GL_CALL(glBindTexture(GL_TEXTURE_2D, wf::gles_texture_t::from_aux(buffers[index]).tex_id));

            auto model = calculate_model_matrix(i);
            program.uniformMatrix4f("model", model);

            if (tessellation_support)
            {
#ifdef USE_GLES32
                GL_CALL(glDrawElements(GL_PATCHES, 6, GL_UNSIGNED_INT, &indexData));
#endif
            } else
            {
                GL_CALL(glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT,
                    &indexData));
            }
        }
    }

    /* Camera position in the cube's model coordinates */
    glm::vec3 camera_position()
    {
        float zoom_factor = animation.cube_animation.zoom;
        auto view_scale   = animation.view * glm::scale(glm::mat4(1.0),
            glm::vec3(1. / zoom_factor, 1. / zoom_factor, 1. / zoom_factor));
        glm::vec4 camera = glm::inverse(view_scale) * glm::vec4(0, 0, 0, 1);
        return glm::vec3(camera) / camera.w;
    }

    /* Brightness of a flat surface with normal @normal and centre @center
     * (model space), as the tessellation shader computes it for the sides
     * when "light" is on (shaders-3-2.tpp); 1 without lighting. */
    float light_factor(glm::vec3 normal, glm::vec3 center)
    {
        if (!tessellation_support || !use_light)
        {
            return 1.0f;
        }

        const glm::vec3 light_source{0, 0, 2};
        float d = glm::distance(center, light_source);
        float ambient = std::pow(std::clamp(2.0f / d, 0.0f, 1.0f), 10.0f);
        float value   = std::clamp(std::pow(std::abs(glm::dot(glm::normalize(normal),
            glm::normalize(light_source))), 1.5f), 0.0f, 1.0f);
        return 0.3f * ambient + 0.7f * value;
    }

    void use_face_program(const glm::mat4& vp)
    {
        static GLfloat vertexData[] = {
            -0.5, 0.5, 0.5, 0.5, 0.5, -0.5, -0.5, -0.5
        };
        static GLfloat coordData[] = {
            0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f
        };

        program.use(wf::TEXTURE_TYPE_RGBA);
        program.attrib_pointer("position", 2, 0, vertexData);
        program.attrib_pointer("uvPosition", 2, 0, coordData);
        program.uniformMatrix4f("VP", vp);
        if (tessellation_support)
        {
            program.uniform1i("deform", use_deform);
            program.uniform1i("light", use_light);
            program.uniform1f("ease", animation.cube_animation.ease_deformation);
        }
    }

    /* 3D windows: draw the sides facing the camera from the farthest to the
     * nearest, each followed by its windows, lifted off the side by
     * level * windows_3d_depth (topmost window highest). Painter's order:
     * no depth buffer needed. Back sides are culled together with their
     * windows (the window quads have the winding of their side). */
    void render_front_sides_3d(const glm::mat4& vp, std::vector<wf::auxilliary_buffer_t>& buffers,
        cube3d::face_windows_t& windows)
    {
        static const GLuint indexData[] = {0, 1, 2, 0, 2, 3};
        const int n    = get_num_faces();
        const auto cws = output->wset()->get_current_workspace();
        const glm::vec3 camera = camera_position();
        const float depth     = std::max(0.0, (double)windows_3d_depth);
        const float thickness = windows_3d_thickness;

        std::vector<int> order(n);
        std::vector<float> dist(n);
        for (int i = 0; i < n; i++)
        {
            order[i] = i;
            dist[i]  = glm::distance(glm::vec3(calculate_model_matrix(i) * glm::vec4(0, 0, 0, 1)), camera);
        }

        std::sort(order.begin(), order.end(), [&] (int a, int b) { return dist[a] > dist[b]; });

        GL_CALL(glEnable(GL_CULL_FACE));
        GL_CALL(glFrontFace(GL_CW));
        for (int i : order)
        {
            const int index  = (cws.x + i) % n;
            const auto model = calculate_model_matrix(i);

            use_face_program(vp);
            GL_CALL(glBindTexture(GL_TEXTURE_2D, wf::gles_texture_t::from_aux(buffers[index]).tex_id));
            program.uniformMatrix4f("model", model);
            if (tessellation_support)
            {
#ifdef USE_GLES32
                GL_CALL(glDrawElements(GL_PATCHES, 6, GL_UNSIGNED_INT, &indexData));
#endif
            } else
            {
                GL_CALL(glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, &indexData));
            }

            program.deactivate();
            if ((index >= (int)windows.size()) || windows[index].empty())
            {
                continue;
            }

            /* lowest window first */
            auto og = output->get_relative_geometry();
            const glm::vec3 normal = glm::vec3(model * glm::vec4(0, 0, 1, 0));
            window_program.use(wf::TEXTURE_TYPE_RGBA);
            window_program.uniformMatrix4f("VP", vp);
            GL_CALL(glEnable(GL_BLEND));
            GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)); /* premultiplied */
            for (auto it = windows[index].rbegin(); it != windows[index].rend(); ++it)
            {
                auto& layer = *it;
                if ((layer->rect.width <= 0) || (layer->rect.height <= 0))
                {
                    continue;
                }

                /* face: x, y in -0.5..0.5, y = -0.5 is the top (v = 0) */
                float x0 = float(layer->rect.x) / og.width - 0.5f;
                float x1 = float(layer->rect.x + layer->rect.width) / og.width - 0.5f;
                float y0 = float(layer->rect.y) / og.height - 0.5f;
                float y1 = float(layer->rect.y + layer->rect.height) / og.height - 0.5f;
                /* same corner order and winding as the side's quad */
                const GLfloat pos[] = {x0, y1, x1, y1, x1, y0, x0, y0};
                const GLfloat uv[]  = {0, 1, 1, 1, 1, 0, 0, 0};
                auto lifted = model * glm::translate(glm::mat4(1.0),
                    glm::vec3(0, 0, depth * layer->level));
                glm::vec3 center = glm::vec3(lifted * glm::vec4((x0 + x1) / 2, (y0 + y1) / 2, 0, 1));

                /* thickness: a slab behind the visible window, not below
                 * the side */
                const float front = depth * layer->level;
                const float back  = std::max(0.0f, front - std::max(0.0f, thickness));
                if ((thickness > 0) && (front > back) && (layer->slab.width > 0))
                {
                    window_program.deactivate();
                    /* the visible window inside the window's texture (which
                     * also holds the shadow) */
                    const auto& r     = layer->rect;
                    const auto& sl    = layer->slab;
                    glm::vec4 slab_uv = {
                        float(sl.x - r.x) / r.width, float(sl.y - r.y) / r.height,
                        float(sl.x + sl.width - r.x) / r.width, float(sl.y + sl.height - r.y) / r.height};
                    glm::vec2 inset = {
                        std::min(float(2.0 / r.width), float((slab_uv.z - slab_uv.x) / 2)),
                        std::min(float(2.0 / r.height), float((slab_uv.w - slab_uv.y) / 2))};
                    render_window_edges(vp, model,
                        float(layer->slab.x) / og.width - 0.5f,
                        float(layer->slab.y) / og.height - 0.5f,
                        float(layer->slab.x + layer->slab.width) / og.width - 0.5f,
                        float(layer->slab.y + layer->slab.height) / og.height - 0.5f,
                        back, front, camera,
                        wf::gles_texture_t::from_aux(layer->buffer).tex_id, slab_uv, inset);
                    window_program.use(wf::TEXTURE_TYPE_RGBA);
                    window_program.uniformMatrix4f("VP", vp);
                }

                window_program.attrib_pointer("position", 2, 0, pos);
                window_program.attrib_pointer("uvPosition", 2, 0, uv);
                window_program.uniformMatrix4f("model", lifted);
                window_program.uniform1f("colorFactor", light_factor(normal, center));
                GL_CALL(glBindTexture(GL_TEXTURE_2D,
                    wf::gles_texture_t::from_aux(layer->buffer).tex_id));
                GL_CALL(glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, &indexData));
            }

            GL_CALL(glDisable(GL_BLEND));
            window_program.deactivate();
        }

        GL_CALL(glDisable(GL_CULL_FACE));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
    }

    /* Thickness of a 3D window: the four edges of a slab from the window
     * (z = front) back towards the side (z = back), in the side's model
     * space. Edges facing away from the camera first, then the others --
     * the window itself is drawn after them (painter's order).
     *
     * With windows_3d_edge_from_window (and a window texture @tex), each
     * edge continues the window's own colours: it stretches the window's
     * outermost row / column of pixels (2 px inside, @uv = the visible
     * window in the texture: u0, v0, u1, v1; v = 0 is the top) back along
     * the slab. Otherwise a flat windows_3d_edge_color. */
    void render_window_edges(const glm::mat4& vp, const glm::mat4& model,
        float x0, float y0, float x1, float y1, float back, float front, const glm::vec3& camera,
        GLuint tex = 0, glm::vec4 uv = {0, 0, 1, 1}, glm::vec2 inset = {0, 0})
    {
        if (tex && windows_3d_edge_from_window)
        {
            render_window_edges_textured(vp, model, x0, y0, x1, y1, back, front, camera, tex, uv, inset);
            return;
        }

        struct edge_t
        {
            glm::vec3 normal;
            GLfloat pos[12];
        };

        const edge_t edges[4] = {
            {{0, -1, 0}, {x0, y0, back, x1, y0, back, x1, y0, front, x0, y0, front}}, /* top */
            {{0, 1, 0}, {x0, y1, back, x1, y1, back, x1, y1, front, x0, y1, front}}, /* bottom */
            {{-1, 0, 0}, {x0, y0, back, x0, y1, back, x0, y1, front, x0, y0, front}}, /* left */
            {{1, 0, 0}, {x1, y0, back, x1, y1, back, x1, y1, front, x1, y0, front}}, /* right */
        };
        static const GLuint indexData[] = {0, 1, 2, 0, 2, 3};

        wf::color_t c = windows_3d_edge_color;
        edge_program.use(wf::TEXTURE_TYPE_RGBA);
        edge_program.uniformMatrix4f("VP", vp);
        edge_program.uniformMatrix4f("model", model);
        edge_program.uniform4f("color", glm::vec4(c.r, c.g, c.b, c.a));
        GL_CALL(glDisable(GL_CULL_FACE));
        for (int pass = 0; pass < 2; pass++)
        {
            for (auto& e : edges)
            {
                glm::vec3 center = glm::vec3(model * glm::vec4(
                    (e.pos[0] + e.pos[6]) / 2, (e.pos[1] + e.pos[7]) / 2, (back + front) / 2, 1));
                glm::vec3 normal  = glm::vec3(model * glm::vec4(e.normal, 0));
                const bool facing = glm::dot(normal, camera - center) > 0;
                if (facing != (pass == 1))
                {
                    continue;
                }

                edge_program.attrib_pointer("position", 3, 0, e.pos);
                edge_program.uniform1f("colorFactor", light_factor(normal, center));
                GL_CALL(glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, &indexData));
            }
        }

        GL_CALL(glEnable(GL_CULL_FACE));
        edge_program.deactivate();
    }

    void render_window_edges_textured(const glm::mat4& vp, const glm::mat4& model,
        float x0, float y0, float x1, float y1, float back, float front, const glm::vec3& camera,
        GLuint tex, glm::vec4 uv, glm::vec2 inset)
    {
        struct edge_t
        {
            glm::vec3 normal;
            GLfloat pos[12];
            GLfloat uv[8];
        };

        const float vt = uv.y + inset.y, vb = uv.w - inset.y; /* top, bottom row */
        const float ul = uv.x + inset.x, ur = uv.z - inset.x; /* left, right column */
        const edge_t edges[4] = {
            {{0, -1, 0}, {x0, y0, back, x1, y0, back, x1, y0, front, x0, y0, front},
                {uv.x, vt, uv.z, vt, uv.z, vt, uv.x, vt}}, /* top */
            {{0, 1, 0}, {x0, y1, back, x1, y1, back, x1, y1, front, x0, y1, front},
                {uv.x, vb, uv.z, vb, uv.z, vb, uv.x, vb}}, /* bottom */
            {{-1, 0, 0}, {x0, y0, back, x0, y1, back, x0, y1, front, x0, y0, front},
                {ul, uv.y, ul, uv.w, ul, uv.w, ul, uv.y}}, /* left */
            {{1, 0, 0}, {x1, y0, back, x1, y1, back, x1, y1, front, x1, y0, front},
                {ur, uv.y, ur, uv.w, ur, uv.w, ur, uv.y}}, /* right */
        };
        static const GLuint indexData[] = {0, 1, 2, 0, 2, 3};

        window_program.use(wf::TEXTURE_TYPE_RGBA);
        window_program.uniformMatrix4f("VP", vp);
        window_program.uniformMatrix4f("model", model);
        GL_CALL(glBindTexture(GL_TEXTURE_2D, tex));
        GL_CALL(glDisable(GL_CULL_FACE));
        for (int pass = 0; pass < 2; pass++)
        {
            for (auto& e : edges)
            {
                glm::vec3 center = glm::vec3(model * glm::vec4(
                    (e.pos[0] + e.pos[6]) / 2, (e.pos[1] + e.pos[7]) / 2, (back + front) / 2, 1));
                glm::vec3 normal  = glm::vec3(model * glm::vec4(e.normal, 0));
                const bool facing = glm::dot(normal, camera - center) > 0;
                if (facing != (pass == 1))
                {
                    continue;
                }

                window_program.attrib_pointer("position", 3, 0, e.pos);
                window_program.attrib_pointer("uvPosition", 2, 0, e.uv);
                window_program.uniform1f("colorFactor", light_factor(normal, center));
                GL_CALL(glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, &indexData));
            }
        }

        GL_CALL(glEnable(GL_CULL_FACE));
        window_program.deactivate();
    }

    void render(const wf::scene::render_instruction_t& data, std::vector<wf::auxilliary_buffer_t>& buffers,
        cube3d::face_windows_t *windows = nullptr)
    {
        data.pass->custom_gles_subpass([&]
        {
            if (program.get_program_id(wf::TEXTURE_TYPE_RGBA) == 0)
            {
                load_program();
            }

            GL_CALL(glClear(GL_DEPTH_BUFFER_BIT));
            background->render_frame(data.target, animation);

            auto vp = calculate_vp_matrix(data.target);

            program.use(wf::TEXTURE_TYPE_RGBA);
            GL_CALL(glEnable(GL_DEPTH_TEST));
            GL_CALL(glDepthFunc(GL_LESS));

            static GLfloat vertexData[] = {
                -0.5, 0.5,
                0.5, 0.5,
                0.5, -0.5,
                -0.5, -0.5
            };

            static GLfloat coordData[] = {
                0.0f, 1.0f,
                1.0f, 1.0f,
                1.0f, 0.0f,
                0.0f, 0.0f
            };

            program.attrib_pointer("position", 2, 0, vertexData);
            program.attrib_pointer("uvPosition", 2, 0, coordData);
            program.uniformMatrix4f("VP", vp);
            if (tessellation_support)
            {
                program.uniform1i("deform", use_deform);
                program.uniform1i("light", use_light);
                program.uniform1f("ease",
                    animation.cube_animation.ease_deformation);
            }

            /* We render the cube in two stages, based on winding.
             * By using two stages, we ensure that we first render the cube sides
             * that are on the back, and then we render those at the front, so we
             * don't have to use depth testing and we also can support alpha cube. */
            if (windows && windows_3d)
            {
                /* 3D windows: the back sides, then the front sides with their windows, which stand out
                 * of the cube */
                GL_CALL(glEnable(GL_CULL_FACE));
                render_cube(GL_CCW, buffers);
                GL_CALL(glDisable(GL_CULL_FACE));
                program.deactivate();
                render_front_sides_3d(vp, buffers, *windows);
            } else
            {
                GL_CALL(glEnable(GL_CULL_FACE));
                render_cube(GL_CCW, buffers);
                render_cube(GL_CW, buffers);
                GL_CALL(glDisable(GL_CULL_FACE));
                program.deactivate();
            }

            GL_CALL(glDisable(GL_DEPTH_TEST));
        });
    }

    wf::effect_hook_t pre_hook = [=] ()
    {
        update_view_matrix();
        wf::scene::damage_node(render_node, render_node->get_bounding_box());
        if (animation.cube_animation.running())
        {
            output->render->schedule_redraw();
        } else if (animation.in_exit)
        {
            deactivate();
        }
    };

    wf::signal::connection_t<wf::input_event_signal<wlr_pointer_motion_event>> on_motion_event =
        [=] (wf::input_event_signal<wlr_pointer_motion_event> *ev)
    {
        rotate_cube(ev->event->delta_x, ev->event->delta_y);
        last_pointer_position = {};

        ev->event->delta_x    = 0;
        ev->event->delta_y    = 0;
        ev->event->unaccel_dx = 0;
        ev->event->unaccel_dy = 0;
    };

    void rotate_cube(double dx, double dy)
    {
        if (animation.in_exit)
        {
            return;
        }

        double xdiff = dx;
        double ydiff = dy * -1.0;

        animation.cube_animation.zoom.restart_with_end(
            animation.cube_animation.zoom.end);

        double current_off_y = animation.cube_animation.offset_y;
        double off_y = current_off_y + ydiff * YVelocity;

        off_y = wf::clamp(off_y, -1.5, 1.5);
        animation.cube_animation.offset_y.set(current_off_y, off_y);
        animation.cube_animation.offset_z.restart_with_end(
            animation.cube_animation.offset_z.end);

        float current_rotation = animation.cube_animation.rotation;
        animation.cube_animation.rotation.restart_with_end(
            current_rotation + xdiff * XVelocity);

        animation.cube_animation.ease_deformation.restart_with_end(
            animation.cube_animation.ease_deformation.end);

        animation.cube_animation.start();
        output->render->schedule_redraw();
    }

    void pointer_scrolled(double amount)
    {
        if (animation.in_exit)
        {
            return;
        }

        animation.cube_animation.offset_y.restart_with_end(
            animation.cube_animation.offset_y.end);
        animation.cube_animation.offset_z.restart_with_end(
            animation.cube_animation.offset_z.end);
        animation.cube_animation.rotation.restart_with_end(
            animation.cube_animation.rotation.end);
        animation.cube_animation.ease_deformation.restart_with_end(
            animation.cube_animation.ease_deformation.end);

        float target_zoom = animation.cube_animation.zoom;
        float start_zoom  = target_zoom;

        target_zoom +=
            std::min(std::pow(target_zoom, 1.5f), ZOOM_MAX) * amount * ZVelocity;
        target_zoom = std::min(std::max(target_zoom, ZOOM_MIN), ZOOM_MAX);
        animation.cube_animation.zoom.set(start_zoom, target_zoom);

        animation.cube_animation.start();
        output->render->schedule_redraw();
    }

    void fini() override
    {
        if (output->is_plugin_active(grab_interface.name))
        {
            deactivate();
        }

        wf::gles::run_in_context_if_gles([&]
        {
            program.free_resources();
            window_program.free_resources();
            edge_program.free_resources();
        });
    }
};


class wayfire_cube_global : public wf::plugin_interface_t,
    public wf::per_output_tracker_mixin_t<wayfire_cube>
{
    wf::ipc_activator_t rotate_left{"cube/rotate_left"};
    wf::ipc_activator_t rotate_right{"cube/rotate_right"};
    wf::ipc_activator_t activate{"cube/activate"};

  public:
    void init() override
    {
        if (!wf::get_core().is_gles2())
        {
            const char *render_type =
                wf::get_core().is_vulkan() ? "vulkan" : (wf::get_core().is_pixman() ? "pixman" : "unknown");
            LOGE("cube: requires GLES2 support, but current renderer is ", render_type);
            return;
        }

        this->init_output_tracking();
        rotate_left.set_handler(rotate_left_cb);
        rotate_right.set_handler(rotate_right_cb);
        activate.set_handler(activate_cb);
    }

    void fini() override
    {
        this->fini_output_tracking();
    }

    wf::ipc_activator_t::handler_t rotate_left_cb = [=] (wf::output_t *output, wayfire_view)
    {
        return this->output_instance[output]->move_vp(-1);
    };

    wf::ipc_activator_t::handler_t rotate_right_cb = [=] (wf::output_t *output, wayfire_view)
    {
        return this->output_instance[output]->move_vp(+1);
    };

    wf::ipc_activator_t::handler_t activate_cb = [=] (wf::output_t *output, wayfire_view)
    {
        return this->output_instance[output]->input_grabbed();
    };
};

DECLARE_WAYFIRE_PLUGIN(wayfire_cube_global);

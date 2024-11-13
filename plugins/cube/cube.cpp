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
#include "plugins/ipc/ipc-activator.hpp"

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
            std::vector<wf::region_t> ws_damage;
            std::vector<wf::render_target_t> framebuffers;

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
                    auto push_damage_child = [=] (const wf::region_t& damage)
                    {
                        ws_damage[i] |= damage;
                        push_damage(self->get_bounding_box());
                    };

                    self->workspaces[i]->gen_render_instances(ws_instances[i],
                        push_damage_child, self->cube->output);

                    ws_damage[i] |= self->workspaces[i]->get_bounding_box();
                }
            }

            ~cube_render_instance_t()
            {
                OpenGL::render_begin();
                for (auto& buf : framebuffers)
                {
                    buf.release();
                }

                OpenGL::render_end();
            }

            void schedule_instructions(
                std::vector<wf::scene::render_instruction_t>& instructions,
                const wf::render_target_t& target, wf::region_t& damage) override
            {
                instructions.push_back(wf::scene::render_instruction_t{
                    .instance = this,
                    .target   = target,
                    .damage   = damage & self->get_bounding_box(),
                });

                auto bbox = self->get_bounding_box();

                damage ^= bbox;
            }

            void render(const wf::render_target_t& target,
                const wf::region_t& region, const std::any& tag) override
            {
                for (int i = 0; i < (int)ws_instances.size(); i++)
                {
                    framebuffers[i].geometry = self->workspaces[i]->get_bounding_box();
                    framebuffers[i].scale    = self->cube->output->handle->scale;
                    framebuffers[i].wl_transform = WL_OUTPUT_TRANSFORM_NORMAL;
                    framebuffers[i].transform    = get_output_matrix_from_transform(
                        framebuffers[i].wl_transform);

                    auto size = framebuffers[i].framebuffer_box_from_geometry_box(framebuffers[i].geometry);
                    OpenGL::render_begin();
                    framebuffers[i].allocate(size.width, size.height);
                    OpenGL::render_end();

                    wf::scene::render_pass_params_t params;
                    params.instances = &ws_instances[i];
                    params.damage    = ws_damage[i];
                    params.reference_output = self->cube->output;
                    params.target = framebuffers[i];
                    wf::scene::run_render_pass(params, wf::scene::RPASS_CLEAR_BACKGROUND |
                        wf::scene::RPASS_EMIT_SIGNALS);

                    ws_damage[i].clear();
                }

                self->cube->render(target.translated(-wf::origin(self->get_bounding_box())), framebuffers);
            }

            void compute_visibility(wf::output_t *output, wf::region_t& visible) override
            {
                for (int i = 0; i < (int)self->workspaces.size(); i++)
                {
                    wf::region_t ws_region = self->workspaces[i]->get_bounding_box();
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
            for (int i = 0; i < w; i++)
            {
                auto node = std::make_shared<wf::workspace_stream_node_t>(cube->output, wf::point_t{i, y});
                workspaces.push_back(node);
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
        std::vector<std::shared_ptr<wf::workspace_stream_node_t>> workspaces;
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

    std::string last_background_mode;
    std::unique_ptr<wf_cube_background_base> background;

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

        OpenGL::render_begin();
        load_program();
        OpenGL::render_end();
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
        render_node = nullptr;
        output->render->rem_effect(&pre_hook);
        output->render->set_require_depth_buffer(false);

        input_grab->ungrab_input();
        output->deactivate_plugin(&grab_interface);
        wf::get_core().unhide_cursor();
        on_motion_event.disconnect();

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

    glm::mat4 calculate_vp_matrix(const wf::render_target_t& dest)
    {
        float zoom_factor = animation.cube_animation.zoom;
        auto scale_matrix = glm::scale(glm::mat4(1.0),
            glm::vec3(1. / zoom_factor, 1. / zoom_factor, 1. / zoom_factor));

        return dest.transform * animation.projection * animation.view * scale_matrix;
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
    void render_cube(GLuint front_face, glm::mat4 fb_transform,
        const std::vector<wf::render_target_t>& buffers)
    {
        GL_CALL(glFrontFace(front_face));
        static const GLuint indexData[] = {0, 1, 2, 0, 2, 3};

        auto cws = output->wset()->get_current_workspace();
        for (int i = 0; i < get_num_faces(); i++)
        {
            int index = (cws.x + i) % get_num_faces();
            GL_CALL(glBindTexture(GL_TEXTURE_2D, buffers[index].tex));

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

    void render(const wf::render_target_t& dest, const std::vector<wf::render_target_t>& buffers)
    {
        if (program.get_program_id(wf::TEXTURE_TYPE_RGBA) == 0)
        {
            load_program();
        }

        OpenGL::render_begin(dest);
        GL_CALL(glClear(GL_DEPTH_BUFFER_BIT));
        OpenGL::render_end();
        background->render_frame(dest, animation);

        auto vp = calculate_vp_matrix(dest);

        OpenGL::render_begin(dest);
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
        GL_CALL(glEnable(GL_CULL_FACE));
        render_cube(GL_CCW, dest.transform, buffers);
        render_cube(GL_CW, dest.transform, buffers);
        GL_CALL(glDisable(GL_CULL_FACE));

        GL_CALL(glDisable(GL_DEPTH_TEST));
        program.deactivate();
        OpenGL::render_end();
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
        pointer_moved(ev->event);

        ev->event->delta_x    = 0;
        ev->event->delta_y    = 0;
        ev->event->unaccel_dx = 0;
        ev->event->unaccel_dy = 0;
    };

    void pointer_moved(wlr_pointer_motion_event *ev)
    {
        if (animation.in_exit)
        {
            return;
        }

        double xdiff = ev->delta_x;
        double ydiff = ev->delta_y;

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

        OpenGL::render_begin();
        program.free_resources();
        OpenGL::render_end();
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

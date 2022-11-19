#include <wayfire/plugin.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/workspace-stream.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-manager.hpp>

#include <wayfire/plugins/common/workspace-stream-sharing.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <wayfire/img.hpp>

#include "cube.hpp"
#include "simple-background.hpp"
#include "skydome.hpp"
#include "cubemap.hpp"
#include "cube-control-signal.hpp"

#define Z_OFFSET_NEAR 0.89567f
#define Z_OFFSET_FAR  2.00000f

#define ZOOM_MAX 10.0f
#define ZOOM_MIN 0.1f

#ifdef USE_GLES32
    #include <GLES3/gl32.h>
#endif

#include "shaders.tpp"
#include "shaders-3-2.tpp"

class wayfire_cube : public wf::plugin_interface_t
{
    wf::button_callback activate_binding;
    wf::activator_callback rotate_left, rotate_right;
    wf::render_hook_t renderer;

    nonstd::observer_ptr<wf::workspace_stream_pool_t> streams;

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

    wf::option_wrapper_t<wf::buttonbinding_t> button{"cube/activate"};
    wf::option_wrapper_t<wf::activatorbinding_t> key_left{"cube/rotate_left"};
    wf::option_wrapper_t<wf::activatorbinding_t> key_right{"cube/rotate_right"};

    std::string last_background_mode;
    std::unique_ptr<wf_cube_background_base> background;

    wf::option_wrapper_t<std::string> background_mode{"cube/background_mode"};

    void reload_background()
    {
        if (!last_background_mode.compare(background_mode))
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
        return output->workspace->get_workspace_grid_size().width;
    }

  public:
    void init() override
    {
        grab_interface->name = "cube";
        grab_interface->capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR;

        animation.cube_animation.offset_y.set(0, 0);
        animation.cube_animation.offset_z.set(0, 0);
        animation.cube_animation.rotation.set(0, 0);
        animation.cube_animation.zoom.set(1, 1);
        animation.cube_animation.ease_deformation.set(0, 0);

        animation.cube_animation.start();

        reload_background();

        activate_binding = [=] (auto)
        {
            return input_grabbed();
        };

        rotate_left = [=] (auto)
        {
            return move_vp(-1);
        };

        rotate_right = [=] (auto)
        {
            return move_vp(1);
        };

        output->add_button(button, &activate_binding);
        output->add_activator(key_left, &rotate_left);
        output->add_activator(key_right, &rotate_right);
        output->connect_signal("cube-control", &on_cube_control);

        grab_interface->callbacks.pointer.button = [=] (uint32_t b, uint32_t s)
        {
            if (s == WL_POINTER_BUTTON_STATE_RELEASED)
            {
                input_ungrabbed();
            }
        };

        grab_interface->callbacks.pointer.axis = [=] (
            wlr_pointer_axis_event *ev)
        {
            if (ev->orientation == WLR_AXIS_ORIENTATION_VERTICAL)
            {
                pointer_scrolled(ev->delta);
            }
        };

        grab_interface->callbacks.cancel = [=] ()
        {
            deactivate();
        };

        auto wsize = output->workspace->get_workspace_grid_size();
        animation.side_angle = 2 * M_PI / float(wsize.width);
        identity_z_offset    = 0.5 / std::tan(animation.side_angle / 2);
        if (wsize.width == 1)
        {
            // tan(M_PI) is 0, so identity_z_offset is invalid
            identity_z_offset = 0.0f;
        }

        animation.cube_animation.offset_z.set(identity_z_offset + Z_OFFSET_NEAR,
            identity_z_offset + Z_OFFSET_NEAR);

        renderer = [=] (const wf::framebuffer_t& dest) {render(dest);};

        OpenGL::render_begin(output->render->get_target_framebuffer());
        load_program();
        OpenGL::render_end();
    }

    void load_program()
    {
#ifdef USE_GLES32
        std::string ext_string(reinterpret_cast<const char*>(glGetString(
            GL_EXTENSIONS)));
        tessellation_support =
            ext_string.find(std::string("GL_EXT_tessellation_shader")) !=
            std::string::npos;
#else
        tessellation_support = false;
#endif

        if (!tessellation_support)
        {
            program.set_simple(OpenGL::compile_program(
                cube_vertex_2_0, cube_fragment_2_0));
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

        streams = wf::workspace_stream_pool_t::ensure_pool(output);
        animation.projection = glm::perspective(45.0f, 1.f, 0.1f, 100.f);
    }

    wf::signal_callback_t on_cube_control = [=] (wf::signal_data_t *data)
    {
        cube_control_signal *d = dynamic_cast<cube_control_signal*>(data);
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
        if (output->is_plugin_active(grab_interface->name))
        {
            return true;
        }

        if (!output->activate_plugin(grab_interface))
        {
            return false;
        }

        wf::get_core().connect_signal("pointer_motion", &on_motion_event);
        output->render->set_renderer(renderer);
        output->render->schedule_redraw();
        wf::get_core().hide_cursor();
        grab_interface->grab();

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
        if (!output->is_plugin_active(grab_interface->name))
        {
            return;
        }

        output->render->set_renderer(nullptr);

        grab_interface->ungrab();
        output->deactivate_plugin(grab_interface);
        wf::get_core().unhide_cursor();
        wf::get_core().disconnect_signal("pointer_motion", &on_motion_event);

        /* Figure out how much we have rotated and switch workspace */
        int size = get_num_faces();
        int dvx  = calculate_viewport_dx_from_rotation();

        auto cws = output->workspace->get_current_workspace();
        int nvx  = (cws.x + (dvx % size) + size) % size;
        output->workspace->set_workspace({nvx, cws.y});

        /* We are finished with rotation, make sure the next time cube is used
         * it is properly reset */
        animation.cube_animation.rotation.set(0, 0);

        for (int i = 0; i < size; i++)
        {
            streams->stop({i, cws.y});
        }
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

        return true;
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

    void update_workspace_streams()
    {
        auto cws = output->workspace->get_current_workspace();
        for (int i = 0; i < get_num_faces(); i++)
        {
            streams->update({i, cws.y});
        }
    }

    glm::mat4 calculate_vp_matrix(const wf::framebuffer_t& dest)
    {
        float zoom_factor = animation.cube_animation.zoom;
        auto scale_matrix = glm::scale(glm::mat4(1.0),
            glm::vec3(1. / zoom_factor, 1. / zoom_factor, 1. / zoom_factor));

        return dest.transform * animation.projection * animation.view * scale_matrix;
    }

    /* Calculate the base model matrix for the i-th side of the cube */
    glm::mat4 calculate_model_matrix(int i, glm::mat4 fb_transform)
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

        return rotation * translation * glm::inverse(fb_transform);
    }

    /* Render the sides of the cube, using the given culling mode - cw or ccw */
    void render_cube(GLuint front_face, glm::mat4 fb_transform)
    {
        GL_CALL(glFrontFace(front_face));
        static const GLuint indexData[] = {0, 1, 2, 0, 2, 3};

        auto cws = output->workspace->get_current_workspace();
        for (int i = 0; i < get_num_faces(); i++)
        {
            int index = (cws.x + i) % get_num_faces();
            GL_CALL(glBindTexture(GL_TEXTURE_2D,
                streams->get({index, cws.y}).buffer.tex));

            auto model = calculate_model_matrix(i, fb_transform);
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

    void render(const wf::framebuffer_t& dest)
    {
        update_workspace_streams();
        if (program.get_program_id(wf::TEXTURE_TYPE_RGBA) == 0)
        {
            load_program();
        }

        OpenGL::render_begin(dest);
        GL_CALL(glClear(GL_DEPTH_BUFFER_BIT));
        OpenGL::render_end();

        reload_background();
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
        render_cube(GL_CCW, dest.transform);
        render_cube(GL_CW, dest.transform);
        GL_CALL(glDisable(GL_CULL_FACE));

        GL_CALL(glDisable(GL_DEPTH_TEST));
        program.deactivate();
        OpenGL::render_end();

        update_view_matrix();

        if (animation.cube_animation.running())
        {
            output->render->schedule_redraw();
        } else if (animation.in_exit)
        {
            deactivate();
        }
    }

    wf::signal_callback_t on_motion_event = [=] (wf::signal_data_t *data)
    {
        auto ev = static_cast<
            wf::input_event_signal<wlr_pointer_motion_event>*>(data);

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
        if (output->is_plugin_active(grab_interface->name))
        {
            deactivate();
        }

        streams->unref();

        OpenGL::render_begin();
        program.free_resources();
        OpenGL::render_end();

        output->rem_binding(&activate_binding);
        output->rem_binding(&rotate_left);
        output->rem_binding(&rotate_right);
        output->disconnect_signal("cube-control", &on_cube_control);
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_cube);

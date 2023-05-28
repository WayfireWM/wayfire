#pragma once

#include "wayfire/signal-definitions.hpp"
#include <memory>
#include <wayfire/object.hpp>
#include <wayfire/output.hpp>
#include <wayfire/geometry.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-stream.hpp>
#include <wayfire/workspace-set.hpp>

namespace wf
{
/**
 * A class which holds one workspace stream per workspace on the given output.
 *
 * Using this interface allows all plugins to use the same OpenGL textures for
 * the workspaces, thereby reducing the memory overhead of a workspace stream.
 */
class workspace_stream_pool_t : public wf::custom_data_t
{
  public:
    /**
     * Make sure there is a stream pool object on the given output, and
     * increase its reference count.
     */
    static nonstd::observer_ptr<workspace_stream_pool_t> ensure_pool(
        wf::output_t *output)
    {
        if (!output->has_data<workspace_stream_pool_t>())
        {
            output->store_data(std::unique_ptr<workspace_stream_pool_t>(
                new workspace_stream_pool_t(output)));
        }

        auto pool = output->get_data<workspace_stream_pool_t>();
        ++pool->ref_count;

        return pool;
    }

    /**
     * Decrease the reference count, and if no more references are being held,
     * then destroy the pool object.
     */
    void unref()
    {
        --ref_count;
        if (ref_count == 0)
        {
            output->erase_data<workspace_stream_pool_t>();
        }
    }

    ~workspace_stream_pool_t()
    {
        resize_pool({0, 0});
    }

    workspace_stream_pool_t(const workspace_stream_pool_t &) = delete;
    workspace_stream_pool_t(workspace_stream_pool_t &&) = delete;
    workspace_stream_pool_t& operator =(const workspace_stream_pool_t&) = delete;
    workspace_stream_pool_t& operator =(workspace_stream_pool_t&&) = delete;

    /**
     * Get the workspace stream for the given workspace
     */
    wf::workspace_stream_t& get(wf::point_t workspace)
    {
        return *streams[workspace.x][workspace.y];
    }

    /**
     * Update the contents of the given workspace.
     *
     * If the workspace has not been started before, it will be started.
     */
    void update(wf::point_t workspace)
    {
        auto& stream = get(workspace);
        if (!stream.current_output)
        {
            stream.start_for_workspace(output, stream.ws);
        }

        stream.render_frame();
    }

    /**
     * Stop the workspace stream.
     */
    void stop(wf::point_t workspace)
    {
        auto& stream = get(workspace);
        stream.stop();
    }

  private:
    workspace_stream_pool_t(wf::output_t *output)
    {
        this->output = output;
        output->connect(&on_workspace_grid_changed);
        resize_pool(this->output->wset()->get_workspace_grid_size());
    }

    void resize_pool(wf::dimensions_t size)
    {
        for (auto& column : this->streams)
        {
            for (auto& stream : column)
            {
                stream->stop();
                OpenGL::render_begin();
                stream->buffer.release();
                OpenGL::render_end();
            }
        }

        this->streams.clear();

        this->streams.resize(size.width);
        for (int i = 0; i < size.width; i++)
        {
            this->streams[i].resize(size.height);
            for (int j = 0; j < size.height; j++)
            {
                this->streams[i][j]     = std::make_unique<workspace_stream_t>();
                this->streams[i][j]->ws = {i, j};
            }
        }
    }

    /** Number of active users of this instance */
    uint32_t ref_count = 0;

    wf::output_t *output;
    std::vector<std::vector<std::unique_ptr<wf::workspace_stream_t>>> streams;

    wf::signal::connection_t<workspace_grid_changed_signal> on_workspace_grid_changed = [=] (auto)
    {
        resize_pool(this->output->wset()->get_workspace_grid_size());
    };
};
}

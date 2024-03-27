#pragma once

#include "wayfire/util.hpp"
#include <memory>
#include <wayfire/scene.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/unstable/translation-node.hpp>

namespace wf
{
/**
 * A subsurface root node. It applies a translation to its children equal to the offset of the subsurface.
 */
class wlr_subsurface_root_node_t : public wf::scene::translation_node_t
{
  public:
    wlr_subsurface_root_node_t(wlr_subsurface *subsurface);
    std::string stringify() const override;
    bool update_offset(bool damage = true);

  private:
    wlr_subsurface *subsurface;
    wf::wl_listener_wrapper on_subsurface_destroy;
    wf::wl_listener_wrapper on_subsurface_commit;
};

/**
 * A class which manages a wlr_subsurface. Its lifetime is tied to the wlr_subsurface object.
 *
 * This class is responsible for managing the subsurface's state and enabling/disabling it when the subsurface
 * is mapped and unmapped. In addition, it should clean up the scenegraph when the subsurface is destroyed.
 */
class wlr_subsurface_controller_t
{
  public:
    wlr_subsurface_controller_t(wlr_subsurface *sub);
    std::shared_ptr<wlr_subsurface_root_node_t> get_subsurface_root();

  private:
    wlr_subsurface *sub;
    wl_listener_wrapper on_map, on_unmap, on_destroy;
    std::shared_ptr<wlr_subsurface_root_node_t> subsurface_root_node;
};
}

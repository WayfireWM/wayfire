#pragma once

#include <wayfire/toplevel.hpp>

namespace wf
{
class xdg_toplevel_t : public toplevel_t
{
  public:
    void commit() override;
    void apply() override;
};
}

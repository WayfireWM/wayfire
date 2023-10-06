#include <wayfire/idle.hpp>
#include <wayfire/util/log.hpp>
#include "core-impl.hpp"

unsigned int wf::idle_inhibitor_t::inhibitors = 0;

void wf::idle_inhibitor_t::notify_wlroots()
{
    /* NOTE: inhibited -> NOT enabled */
    wlr_idle_set_enabled(wf::get_core().protocols.idle, NULL, inhibitors == 0);
}

wf::idle_inhibitor_t::idle_inhibitor_t()
{
    LOGD("creating idle inhibitor ", this, " previous count: ", inhibitors);
    inhibitors++;
    notify_wlroots();
}

wf::idle_inhibitor_t::~idle_inhibitor_t()
{
    LOGD("destroying idle inhibitor ", this, " previous count: ", inhibitors);
    inhibitors--;
    notify_wlroots();
}

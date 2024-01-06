#include <wayfire/idle.hpp>
#include <wayfire/util/log.hpp>
#include "core/seat/input-manager.hpp"
#include "core-impl.hpp"

unsigned int wf::idle_inhibitor_t::inhibitors = 0;

void wf::idle_inhibitor_t::notify_update()
{
    /* NOTE: inhibited -> NOT enabled */
    wlr_idle_notifier_v1_set_inhibited(wf::get_core().protocols.idle_notifier, inhibitors != 0);

    wf::idle_inhibit_changed_signal data;
    data.inhibit = (inhibitors != 0);
    wf::get_core().emit(&data);
}

wf::idle_inhibitor_t::idle_inhibitor_t()
{
    LOGD("creating idle inhibitor ", this, " previous count: ", inhibitors);
    inhibitors++;
    notify_update();
}

wf::idle_inhibitor_t::~idle_inhibitor_t()
{
    LOGD("destroying idle inhibitor ", this, " previous count: ", inhibitors);
    inhibitors--;
    notify_update();
}

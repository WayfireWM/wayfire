#pragma once

namespace wf
{
/**
 * Dummy non-copyable type that increments the global inhibitor count when created,
 * and decrements when destroyed. These changes influence wlroots idle enablement.
 */
class idle_inhibitor_t
{
  public:
    idle_inhibitor_t();
    ~idle_inhibitor_t();

    idle_inhibitor_t(const idle_inhibitor_t &) = delete;
    idle_inhibitor_t(idle_inhibitor_t &&) = delete;
    idle_inhibitor_t& operator =(const idle_inhibitor_t&) = delete;
    idle_inhibitor_t& operator =(idle_inhibitor_t&&) = delete;

  private:
    static unsigned int inhibitors;
    void notify_wlroots();
};
}

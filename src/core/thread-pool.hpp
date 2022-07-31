#ifndef WF_CORE_THREAD_POOL_HPP
#define WF_CORE_THREAD_POOL_HPP

#include "BS_thread_pool.hpp"

namespace wf
{

class thread_pool: public BS::thread_pool
{
public:
  using BS::thread_pool::thread_pool;

  std::future<void> submit_task(std::function<void(void *)> &task, void *args)
  {
    return submit(task, args);
  }
};

}

#endif /* end of include guard: WF_CORE_THREAD_POOL_HPP */

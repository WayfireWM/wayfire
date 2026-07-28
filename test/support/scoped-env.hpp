#pragma once

#include <cstdlib>
#include <string>
#include <utility>

namespace wf::test
{
class scoped_env_t
{
    std::string name;
    std::string old_value;
    bool had_old_value = false;

  public:
    scoped_env_t(std::string name, std::string value) : name(std::move(name))
    {
        if (const char *old = getenv(this->name.c_str()))
        {
            had_old_value = true;
            old_value     = old;
        }

        setenv(this->name.c_str(), value.c_str(), 1);
    }

    ~scoped_env_t()
    {
        if (had_old_value)
        {
            setenv(name.c_str(), old_value.c_str(), 1);
        } else
        {
            unsetenv(name.c_str());
        }
    }
};
}

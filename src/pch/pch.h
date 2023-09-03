#include <functional>
#include <memory>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <list>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <wayland-server.h>
#include <wayland-server-protocol.h>
#include <wayland-server-core.h>
#include <glm/glm.hpp>

#include <wayfire/view.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/signal-provider.hpp>
#include <wayfire/object.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/nonstd/observer_ptr.h>

#include "src/core/core-impl.hpp"
#include "src/view/view-impl.hpp"

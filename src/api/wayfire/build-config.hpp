#pragma once

// WF_USE_CONFIG_H is set only when building Wayfire itself, external plugins
// need to use <wayfire/config.h>
#ifdef WF_USE_CONFIG_H
    #include <config.h>
#else
    #include <wayfire/config.h>
#endif

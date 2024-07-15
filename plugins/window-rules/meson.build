window_rules  = shared_module('window-rules',
                              ['window-rules.cpp', 'view-action-interface.cpp'],
                              include_directories: [wayfire_api_inc, wayfire_conf_inc, grid_inc, plugins_common_inc],
                              dependencies: [wlroots, pixman, wfconfig, wfutils, plugin_pch_dep],
                              install: true,
                              install_dir: join_paths(get_option('libdir'), 'wayfire'))

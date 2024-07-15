animiate = shared_module('cube',
                         ['cube.cpp', 'cubemap.cpp', 'skydome.cpp', 'simple-background.cpp'],
                         include_directories: [wayfire_api_inc, wayfire_conf_inc, plugins_common_inc, ipc_include_dirs],
                         dependencies: [wlroots, pixman, wfconfig, json, plugin_pch_dep],
                         install: true,
                         install_dir: join_paths(get_option('libdir'), 'wayfire'))

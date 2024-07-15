tile = shared_module('simple-tile',
        ['tile-plugin.cpp', 'tree.cpp', 'tree-controller.cpp'],
        include_directories: [wayfire_api_inc, wayfire_conf_inc, plugins_common_inc, grid_inc, wobbly_inc, ipc_include_dirs],
        dependencies: [wlroots, pixman, wfconfig, json, plugin_pch_dep],
        cpp_args: plugin_pch_args,
        install: true,
        install_dir: join_paths(get_option('libdir'), 'wayfire'))



tile = shared_module('simple-tile',
        ['tile-plugin.cpp', 'tree.cpp', 'tree-controller.cpp'],
        include_directories: [wayfire_api_inc, wayfire_conf_inc, plugins_common_inc, grid_inc, wobbly_inc, ipc_include_dirs],
        dependencies: [wlroots, pixman, wfconfig, json, plugin_pch_dep],
        link_with: [move_drag_interface],
        install: true,
        install_dir: join_paths(get_option('libdir'), 'wayfire'))



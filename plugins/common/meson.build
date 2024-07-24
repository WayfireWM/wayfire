plugins_common_inc = include_directories('.')
install_subdir('wayfire', install_dir: get_option('includedir'))

workspace_wall = static_library('wayfire-workspace-wall',
     ['workspace-wall.cpp'],
     include_directories: [wayfire_api_inc, wayfire_conf_inc],
     dependencies: [wlroots, pixman, wfconfig, plugin_pch_dep],
     override_options: ['b_lundef=false'],
     install: true)

move_drag_interface = static_library('wayfire-move-drag-interface',
     ['move-drag-interface.cpp'],
     include_directories: [wayfire_api_inc, wayfire_conf_inc, wobbly_inc],
     dependencies: [wlroots, pixman, wfconfig, plugin_pch_dep],
     override_options: ['b_lundef=false'],
     install: true)

plugins_common_inc = include_directories('.')
install_subdir('wayfire', install_dir: get_option('includedir'))

workspace_wall = static_library('wayfire-workspace-wall',
     ['workspace-wall.cpp'],
     include_directories: [wayfire_api_inc, wayfire_conf_inc],
     dependencies: [wlroots, pixman, wfconfig, plugin_pch_dep],
     cpp_args: plugin_pch_args,
     override_options: ['b_lundef=false'],
     install: true)


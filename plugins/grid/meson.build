grid_inc = include_directories('.')
all_include_dirs = [wayfire_api_inc, wayfire_conf_inc, plugins_common_inc, wobbly_inc, grid_inc]
all_deps = [wlroots, pixman, wfconfig, wftouch, cairo, json, plugin_pch_dep]

shared_module('grid', ['grid.cpp'],
        include_directories: all_include_dirs,
        dependencies: all_deps,
        install: true,
        install_dir: conf_data.get('PLUGIN_PATH'))

install_subdir('wayfire', install_dir: get_option('includedir'))

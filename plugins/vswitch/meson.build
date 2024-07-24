vswitch_inc = include_directories('.')

vswitch = shared_module('vswitch',
    ['vswitch.cpp'],
    include_directories: [wayfire_api_inc, wayfire_conf_inc, plugins_common_inc, vswitch_inc, ipc_include_dirs],
    dependencies: [wlroots, pixman, wfconfig, json, plugin_pch_dep],
    link_with: [workspace_wall],
    install: true,
    install_dir: join_paths(get_option('libdir'), 'wayfire'))

install_headers(['wayfire/plugins/vswitch.hpp'], subdir: 'wayfire/plugins')
